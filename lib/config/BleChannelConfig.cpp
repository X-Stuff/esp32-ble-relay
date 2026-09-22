#include "BleChannelConfig.h"

#include "JsonMembers.h"

namespace w2b
{

namespace
{

constexpr const char* kModes[] = {"uuid", "none"};
constexpr const char* kSubscribes[] = {"auto", "indicate", "notify"};

bool readUuid(const JsonObjectConst& json, BleUuid& uuid, std::string& error)
{
	const char* text = nullptr;

	if (json["uuid"].isNull())
	{
		return true;
	}

	if (!readText(json, "uuid", text, error))
	{
		return false;
	}

	if (!BleUuid::parse(text, uuid))
	{
		error = "uuid: \"" + std::string(text) + "\" is not a GATT UUID, e.g. \"fed5\" or \"0000fed5-0000-1000-8000-00805f9b34fb\"";
		return false;
	}

	return true;
}

bool validateChannel(ChannelMode mode, const BleUuid& uuid, std::string& error)
{
	if (mode != ChannelMode::Uuid && !uuid.empty())
	{
		error = std::string("uuid: must be empty with mode ") + choiceName(mode, kModes);
		return false;
	}

	if (mode == ChannelMode::Uuid && uuid.empty())
	{
		error = "uuid: missing, mode uuid needs one, e.g. \"fed5\"";
		return false;
	}

	return true;
}

}  // namespace

TxConfig::TxConfig(ChannelMode mode, const BleUuid& uuid, bool writeWithResponse)
	: channelMode(mode), characteristic(uuid), withResponse(writeWithResponse)
{
}

bool TxConfig::validate(std::string& error) const
{
	return validateChannel(channelMode, characteristic, error);
}

void TxConfig::toJson(JsonObject& json) const
{
	json["mode"] = choiceName(channelMode, kModes);
	json["uuid"] = characteristic.text().c_str();
	json["write_with_response"] = withResponse;
}

bool TxConfig::fromJson(const JsonObjectConst& json, std::string& error)
{
	return readOptionalChoice(json, "mode", channelMode, kModes, error) && readUuid(json, characteristic, error) &&
		readOptionalBool(json, "write_with_response", withResponse, error) && validate(error);
}

bool TxConfig::operator==(const TxConfig& other) const
{
	return channelMode == other.channelMode && characteristic == other.characteristic && withResponse == other.withResponse;
}

RxConfig::RxConfig(ChannelMode mode, const BleUuid& uuid, Subscribe subscribe)
	: channelMode(mode), characteristic(uuid), subscription(subscribe)
{
}

bool RxConfig::validate(std::string& error) const
{
	return validateChannel(channelMode, characteristic, error);
}

void RxConfig::toJson(JsonObject& json) const
{
	json["mode"] = choiceName(channelMode, kModes);
	json["uuid"] = characteristic.text().c_str();
	json["subscribe"] = choiceName(subscription, kSubscribes);
}

bool RxConfig::fromJson(const JsonObjectConst& json, std::string& error)
{
	return readOptionalChoice(json, "mode", channelMode, kModes, error) && readUuid(json, characteristic, error) &&
		readOptionalChoice(json, "subscribe", subscription, kSubscribes, error) && validate(error);
}

bool RxConfig::operator==(const RxConfig& other) const
{
	return channelMode == other.channelMode && characteristic == other.characteristic && subscription == other.subscription;
}

}  // namespace w2b
