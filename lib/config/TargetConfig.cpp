#include "TargetConfig.h"

#include "ConfigRules.h"
#include "JsonMembers.h"

namespace w2b
{

TargetConfig::TargetConfig(const std::optional<BleAddress>& address, const Name& name)
	: peer(address), advertisedName(name)
{
}

bool TargetConfig::validate(std::string& error) const
{
	if (!advertisedName.fits() || !isValidLabel(advertisedName.c_str()))
	{
		error = "name: at most 63 bytes, no control characters";
		return false;
	}

	if (!peer && advertisedName.empty())
	{
		error = "address: missing, and no name to find the device by";
		return false;
	}

	return true;
}

void TargetConfig::toJson(JsonObject& json) const
{
	json["address"] = peer ? peer->text().c_str() : "";
	json["name"] = advertisedName.c_str();
}

bool TargetConfig::fromJson(const JsonObjectConst& json, std::string& error)
{
	return readAddress(json, error) && readOptionalString(json, "name", advertisedName, error) && validate(error);
}

bool TargetConfig::operator==(const TargetConfig& other) const
{
	return peer == other.peer && advertisedName == other.advertisedName;
}

bool TargetConfig::readAddress(const JsonObjectConst& json, std::string& error)
{
	const char* address = nullptr;

	if (!readOptionalText(json, "address", address, error))
	{
		return false;
	}

	if (address == nullptr)
	{
		return true;
	}

	if (*address == '\0')
	{
		peer.reset();
		return true;
	}

	BleAddress parsed;

	if (!BleAddress::parse(address, parsed))
	{
		error = "address: \"" + std::string(address) + "\" is not a BLE address, e.g. \"12:34:56:78:9a:bc\"";
		return false;
	}

	peer = parsed;

	return true;
}

}  // namespace w2b
