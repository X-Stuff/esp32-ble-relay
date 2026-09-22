#include "BleConnectionManager.h"

#include <algorithm>

#include <NimBLEDevice.h>

#include "BleScanner.h"
#include "Config.h"
#include "Logger.h"
#include "NimBleConvert.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "ble";

}  // namespace

BleConnectionManager::BleConnectionManager(const Config& config, BleScanner& scanner)
	: config(config), scanner(scanner)
{
}

// The relay's own address as BleService chose it from the configuration, the one its log line names.
void BleConnectionManager::setup()
{
	if (!NimBLEDevice::isInitialized())
	{
		Logger::error(kTag, "NimBLE is not running; no relay connects to its device");
		return;
	}

	const NimBLEAddress address = NimBLEDevice::getAddress();
	own = toBleAddress(address);
	ownType = toAddressType(address);

	for (BleConnection& connection : connections)
	{
		connection.init(scanner, address.getType());
	}

	nimBleRunning = true;
}

void BleConnectionManager::update(uint32_t dtMs)
{
	if (!nimBleRunning)
	{
		return;
	}

	const bool changed = applyConfig();
	assignWaitingDevices();

	for (BleConnection& connection : connections)
	{
		connection.update(dtMs);
	}

	sinceStatusMs += dtMs;

	if (changed || sinceStatusMs >= kStatusPeriodMs)
	{
		sinceStatusMs = 0;
		updateStatuses();
	}
}

void BleConnectionManager::readStatuses(const std::function<void(const std::vector<RelayStatus>&)>& reader) const
{
	LockGuard guard(&lock);
	reader(lastStatuses);
}

bool BleConnectionManager::applyConfig()
{
	const uint32_t revision = config.currentRevision();

	if (revision == appliedRevision)
	{
		return false;
	}

	appliedRevision = revision;
	config.read([this](const ConfigData& data)
		{
			rules = data.rules();
		});

	const std::vector<DeviceRules> devices = DeviceRules::groupByDevice(rules);
	waitingDevices.clear();

	for (BleConnection& connection : connections)
	{
		if (connection.device() && std::find(devices.begin(), devices.end(), *connection.device()) == devices.end())
		{
			connection.clearDevice();
		}
	}

	for (const DeviceRules& device : devices)
	{
		const bool hasConnection = std::any_of(connections.begin(), connections.end(),
			[&device](const BleConnection& connection)
			{
				return connection.device() && *connection.device() == device;
			});

		if (!hasConnection)
		{
			waitingDevices.push_back(device);
		}
	}

	return true;
}

// A closing connection may hold a link to a waiting device, which NimBLE would refuse to connect again; so all wait until it is down.
void BleConnectionManager::assignWaitingDevices()
{
	const bool closing = std::any_of(connections.begin(), connections.end(),
		[](const BleConnection& connection)
		{
			return connection.isClosing();
		});

	if (closing)
	{
		return;
	}

	for (auto device = waitingDevices.begin(); device != waitingDevices.end();)
	{
		auto connection = std::find_if(connections.begin(), connections.end(),
			[](const BleConnection& candidate)
			{
				return candidate.isFree();
			});

		if (connection == connections.end())
		{
			return;
		}

		connection->setDevice(*device);
		device = waitingDevices.erase(device);
	}
}

// A rule without a connection yet shows idle, a disabled rule disabled.
void BleConnectionManager::updateStatuses()
{
	nextStatuses.clear();

	for (const RuleConfig& rule : rules)
	{
		std::optional<RelayStatus> status;

		for (const BleConnection& connection : connections)
		{
			if (connection.readStatus(rule.id(), status))
			{
				break;
			}
		}

		if (!status)
		{
			status = RelayStatus(rule.id(), rule.name(), rule.enabled() ? LinkState::Idle : LinkState::Disabled, rule.target().address(), 0, 0, 0,
				RelayStatus::Error(), RelayCounters(), TcpStatus());
		}

		nextStatuses.push_back(*status);
	}

	LockGuard guard(&lock);
	lastStatuses.swap(nextStatuses);
}

}  // namespace w2b
