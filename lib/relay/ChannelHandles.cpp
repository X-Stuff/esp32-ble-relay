#include "ChannelHandles.h"

#include <algorithm>

namespace w2b
{

namespace
{

// the property bits a relay needs
constexpr uint8_t kWriteNoResponse = 0x04;

constexpr uint8_t kWrite = 0x08;

constexpr uint8_t kNotify = 0x10;

constexpr uint8_t kIndicate = 0x20;

const DiscoveredCharacteristic* findCharacteristic(const std::vector<DiscoveredCharacteristic>& characteristics, const BleUuid& uuid)
{
	auto found = std::find_if(characteristics.begin(), characteristics.end(),
		[&uuid](const DiscoveredCharacteristic& characteristic)
		{
			return characteristic.uuid().equals(uuid);
		});

	return found != characteristics.end() ? &*found : nullptr;
}

std::string formatNotInService(const char* channel, const RuleConfig& rule, const BleUuid& uuid)
{
	return std::string(channel) + " " + uuid.text().c_str() + ": not in service " + rule.serviceUuid().text().c_str();
}

}  // namespace

DiscoveredCharacteristic::DiscoveredCharacteristic(const BleUuid& uuid, uint8_t properties, uint16_t valueHandle, uint16_t endHandle)
	: id(uuid), bits(properties), handle(valueHandle), lastHandle(endHandle)
{
}

bool ChannelHandles::find(const RuleConfig& rule, const std::vector<DiscoveredCharacteristic>& characteristics, ChannelHandles& handles,
	std::string& error)
{
	ChannelHandles found;

	if (!findTx(rule, characteristics, found, error) || !findRx(rule, characteristics, found, error))
	{
		return false;
	}

	handles = found;

	return true;
}

bool ChannelHandles::findTx(const RuleConfig& rule, const std::vector<DiscoveredCharacteristic>& characteristics, ChannelHandles& handles,
	std::string& error)
{
	if (rule.tx().mode() == ChannelMode::None)
	{
		return true;
	}

	const DiscoveredCharacteristic* found = findCharacteristic(characteristics, rule.tx().uuid());

	if (found == nullptr)
	{
		error = formatNotInService("tx", rule, rule.tx().uuid());
		return false;
	}

	if ((found->properties() & (kWrite | kWriteNoResponse)) == 0)
	{
		error = std::string("tx ") + rule.tx().uuid().text().c_str() + ": neither write nor write without response";
		return false;
	}

	handles.tx = found->valueHandle();
	handles.withResponse = rule.tx().writeWithResponse() ? (found->properties() & kWrite) != 0 : (found->properties() & kWriteNoResponse) == 0;

	return true;
}

bool ChannelHandles::findRx(const RuleConfig& rule, const std::vector<DiscoveredCharacteristic>& characteristics, ChannelHandles& handles,
	std::string& error)
{
	if (rule.rx().mode() == ChannelMode::None)
	{
		return true;
	}

	const DiscoveredCharacteristic* found = findCharacteristic(characteristics, rule.rx().uuid());
	const std::string channel = std::string("rx ") + rule.rx().uuid().text().c_str();

	if (found == nullptr)
	{
		error = formatNotInService("rx", rule, rule.rx().uuid());
		return false;
	}

	const bool canIndicate = (found->properties() & kIndicate) != 0;
	const bool canNotify = (found->properties() & kNotify) != 0;
	const RxConfig::Subscribe asked = rule.rx().subscribe();

	if ((asked == RxConfig::Subscribe::Indicate && !canIndicate) || (asked == RxConfig::Subscribe::Notify && !canNotify) || (!canIndicate && !canNotify))
	{
		const char* missing = asked == RxConfig::Subscribe::Indicate ? ": no indicate"
			: asked == RxConfig::Subscribe::Notify ? ": no notify" : ": neither notify nor indicate";
		error = channel + missing;
		return false;
	}

	handles.rx = found->valueHandle();
	handles.rxEndHandle = found->endHandle();
	handles.indicate = asked == RxConfig::Subscribe::Indicate || (asked == RxConfig::Subscribe::Auto && canIndicate);

	return true;
}

bool ChannelHandles::operator==(const ChannelHandles& other) const
{
	return tx == other.tx && withResponse == other.withResponse && rx == other.rx && rxEndHandle == other.rxEndHandle && indicate == other.indicate;
}

}  // namespace w2b
