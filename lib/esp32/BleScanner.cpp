#include "BleScanner.h"

#include <algorithm>
#include <string>

#include <NimBLEDevice.h>

#include "BleConnection.h"
#include "Logger.h"
#include "NimBleConvert.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "ble";

// a scan window of half of each interval leaves the radio to WiFi the other half
constexpr uint16_t kScanIntervalMs = 100;

constexpr uint16_t kScanWindowMs = 50;

}  // namespace

const char* BleScanner::stateStr(State state)
{
	switch (state)
	{
		case State::Idle:      return "idle";
		case State::Requested: return "requested";
		case State::Running:
		case State::Ended:     return "running";
		case State::Done:      return "done";
		case State::Failed:    return "failed";
	}

	return "?";
}

void BleScanner::setup()
{
	if (!NimBLEDevice::isInitialized())
	{
		Logger::error(kTag, "NimBLE is not running; every BLE scan fails");
		return;
	}

	NimBLEScan* scan = NimBLEDevice::getScan();
	const bool wantDuplicates = true;

	// every report, so a device's later scan response and stronger signal reach the list; NimBLE stores none of them
	scan->setScanCallbacks(this, wantDuplicates);
	scan->setMaxResults(0);
	scan->setActiveScan(true);
	scan->setInterval(kScanIntervalMs);
	scan->setWindow(kScanWindowMs);
}

void BleScanner::update(uint32_t dtMs)
{
	switch (currentState())
	{
		case State::Requested:
		{
			startScan(dtMs);
			break;
		}
		case State::Ended:
		{
			finishScan();
			break;
		}
		case State::Idle:
		case State::Running:
		case State::Done:
		case State::Failed:
		{
			break;
		}
	}

	updateSearches(dtMs);
}

BleScanner::State BleScanner::request(uint32_t seconds)
{
	LockGuard guard(&lock);

	if (state == State::Idle || state == State::Done || state == State::Failed)
	{
		requestedSeconds = std::min(std::max<uint32_t>(seconds, 1), kMaxSeconds);
		radioWaitedMs = 0;
		state = State::Requested;
	}

	return state;
}

BleScanner::State BleScanner::read(BleScanList& devices) const
{
	LockGuard guard(&lock);
	devices = scanList;

	return state;
}

bool BleScanner::clearDevices()
{
	LockGuard guard(&lock);

	if (isPageScanActive())
	{
		return false;
	}

	// an empty list, since clear() keeps the memory
	scanList = BleScanList();
	state = State::Idle;

	return true;
}

uint32_t BleScanner::seconds() const
{
	LockGuard guard(&lock);

	return requestedSeconds;
}

BleScanner::State BleScanner::currentState() const
{
	LockGuard guard(&lock);

	return state;
}

size_t BleScanner::startSearch(const TargetConfig& target)
{
	LockGuard guard(&lock);

	for (size_t slot = 0; slot < searches.size(); ++slot)
	{
		if (searches[slot].state == SearchState::Free)
		{
			searches[slot] = Search{.state = SearchState::Searching, .target = target};
			return slot;
		}
	}

	return kMaxSearches;
}

BleScanner::SearchState BleScanner::readSearch(size_t slot, std::optional<BleDeviceBase>& device, bool& duplicateName) const
{
	LockGuard guard(&lock);

	if (slot >= searches.size())
	{
		return SearchState::Free;
	}

	device = searches[slot].device;
	duplicateName = searches[slot].duplicateName;

	return searches[slot].state;
}

void BleScanner::stopSearch(size_t slot)
{
	LockGuard guard(&lock);

	if (slot < searches.size())
	{
		searches[slot] = Search();
	}
}

// NimBLE's host has its HCI command to stop scanning answered before stop() returns, so a connect may start right after.
bool BleScanner::acquireRadio(const void* holder)
{
	bool stopSearchScan = false;

	{
		LockGuard guard(&lock);

		if (radioHolder != nullptr && radioHolder != holder)
		{
			return false;
		}

		radioHolder = holder;
		stopSearchScan = searchScanRunning;
		searchScanRunning = false;
	}

	// outside the lock, which NimBLE's host task may be waiting for in onResult()
	if (stopSearchScan)
	{
		NimBLEDevice::getScan()->stop();
	}

	return true;
}

void BleScanner::releaseRadio(const void* holder)
{
	LockGuard guard(&lock);

	if (radioHolder == holder)
	{
		radioHolder = nullptr;
	}
}

void BleScanner::onResult(const NimBLEAdvertisedDevice* advertised)
{
	const NimBLEAddress& address = advertised->getAddress();
	BleDevice report(toBleAddress(address), toAddressType(address), advertised->isConnectable(), advertised->getName().c_str(),
		advertised->getRSSI());

	for (uint8_t i = 0; i < advertised->getServiceUUIDCount(); ++i)
	{
		report.addService(toBleUuid(advertised->getServiceUUID(i)));
	}

	const std::string maker = advertised->getManufacturerData();
	report.setManufacturerData(reinterpret_cast<const uint8_t*>(maker.data()), maker.size());

	LockGuard guard(&lock);

	if (state == State::Running)
	{
		scanList.add(report);
	}

	matchSearches(report);
}

void BleScanner::onScanEnd(const NimBLEScanResults& /*results*/, int reason)
{
	LockGuard guard(&lock);

	if (state == State::Running)
	{
		endReason = reason;
		state = State::Ended;
		return;
	}

	// NimBLE stopped the search scan; updateSearches() starts it again
	searchScanRunning = false;
}

// By address: the first report. By name: the strongest device, and a note when another device has the same name.
void BleScanner::matchSearches(const BleDevice& report)
{
	for (Search& search : searches)
	{
		if (search.state != SearchState::Searching)
		{
			continue;
		}

		const TargetConfig& target = *search.target;

		if (target.address())
		{
			if (*target.address() == report.address())
			{
				search.device = BleDeviceBase(report);
				search.state = SearchState::Found;
			}

			continue;
		}

		if (report.name().empty() || !(report.name() == target.name().c_str()))
		{
			continue;
		}

		search.duplicateName = search.duplicateName || (search.device && search.device->address() != report.address());

		if (!search.device || report.rssi() > search.device->rssi())
		{
			search.device = BleDeviceBase(report);
		}
	}
}

// Active: NimBLE sends a scan request to every scannable device it sees, for its scan response with the name.
void BleScanner::startScan(uint32_t dtMs)
{
	// the page scan replaces the search scan, and the searches see its reports too
	if (!acquireRadio(this))
	{
		radioWaitedMs += dtMs;

		if (radioWaitedMs >= BleConnection::kRadioWaitMs)
		{
			setState(State::Failed);
			Logger::warn(kTag, "the BLE scan did not start: connects kept the radio for %u s", (unsigned)(BleConnection::kRadioWaitMs / 1000));
		}

		return;
	}

	{
		LockGuard guard(&lock);
		scanList.clear();
		// before NimBLE starts: it may report at once
		state = State::Running;
	}

	const bool isContinue = false;
	// a scan NimBLE still runs is restarted with this one's length
	const bool restart = true;

	if (!NimBLEDevice::isInitialized() || !NimBLEDevice::getScan()->start(requestedSeconds * 1000, isContinue, restart))
	{
		releaseRadio(this);
		setState(State::Failed);
		Logger::warn(kTag, "the BLE scan did not start");
		return;
	}

	Logger::info(kTag, "BLE scan: active, %u s", (unsigned)requestedSeconds);
}

void BleScanner::finishScan()
{
	releaseRadio(this);

	if (endReason == 0)
	{
		setState(State::Done);
		Logger::info(kTag, "BLE scan done: %u devices", (unsigned)scanList.devices().size());
		return;
	}

	setState(State::Failed);
	Logger::warn(kTag, "NimBLE stopped the BLE scan early, reason %d; %u devices found", endReason, (unsigned)scanList.devices().size());
}

// The search scan has no time limit; it stops when no search is running.
void BleScanner::updateSearches(uint32_t dtMs)
{
	bool start = false;
	bool stop = false;

	{
		LockGuard guard(&lock);
		const bool scanning = searchScanRunning || state == State::Running;
		bool searching = false;

		for (Search& search : searches)
		{
			if (search.state == SearchState::Searching && scanning)
			{
				updateTimings(search, dtMs);
			}

			searching = searching || search.state == SearchState::Searching;
		}

		start = searching && !searchScanRunning && radioHolder == nullptr;
		stop = !searching && searchScanRunning;
		searchScanRunning = (searchScanRunning && !stop) || start;
	}

	if (stop)
	{
		NimBLEDevice::getScan()->stop();
	}

	const uint32_t forever = 0;
	const bool isContinue = false;
	const bool restart = false;

	if (start && (!NimBLEDevice::isInitialized() || !NimBLEDevice::getScan()->start(forever, isContinue, restart)))
	{
		LockGuard guard(&lock);
		searchScanRunning = false;
	}
}

void BleScanner::updateTimings(Search& search, uint32_t dtMs)
{
	search.searchedMs += dtMs;

	if (search.device)
	{
		search.sinceFirstMatchMs += dtMs;
	}

	if (search.device && search.sinceFirstMatchMs >= kNameWaitMs)
	{
		search.state = SearchState::Found;
	}
	else if (search.searchedMs >= kSearchTimeoutMs)
	{
		search.state = SearchState::NotFound;
	}
}

bool BleScanner::isPageScanActive() const
{
	return state == State::Requested || state == State::Running || state == State::Ended;
}

void BleScanner::setState(State next)
{
	LockGuard guard(&lock);
	state = next;
}

}  // namespace w2b
