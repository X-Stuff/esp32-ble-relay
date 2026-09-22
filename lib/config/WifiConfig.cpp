#include "WifiConfig.h"

#include "ConfigRules.h"
#include "JsonMembers.h"

namespace w2b
{

WifiConfig::WifiConfig(const SSID& ssid, const Password& password, const std::optional<StaticIpConfig>& staticIp)
	: network(ssid), passphrase(password), address(staticIp)
{
}

bool WifiConfig::validate(std::string& error) const
{
	if (!network.fits())
	{
		error = "ssid: longer than 32 bytes";
		return false;
	}

	if (!passphrase.fits() || !isValidWifiPassword(passphrase.c_str()))
	{
		error = "password: must be empty, 8-63 printable ASCII characters, or 64 hex digits";
		return false;
	}

	if (address && !address->validate(error))
	{
		error = "static." + error;
		return false;
	}

	return true;
}

void WifiConfig::toJson(JsonObject& json) const
{
	json["ssid"] = network.c_str();
	json["password"] = passphrase.c_str();

	if (address)
	{
		writeObject(json, "static", *address);
	}
	else
	{
		json["static"] = nullptr;
	}
}

bool WifiConfig::fromJson(const JsonObjectConst& json, std::string& error)
{
	return readOptionalString(json, "ssid", network, error) && readOptionalString(json, "password", passphrase, error) &&
		readStaticIp(json, error) && validate(error);
}

bool WifiConfig::readStaticIp(const JsonObjectConst& json, std::string& error)
{
	if (isMissing(json, "static"))
	{
		return true;
	}

	if (json["static"].isNull())
	{
		address.reset();
		return true;
	}

	StaticIpConfig staticIp = SerializableFactory::createEmpty<StaticIpConfig>();

	if (!readObject(json, "static", staticIp, error))
	{
		return false;
	}

	address = staticIp;

	return true;
}

bool WifiConfig::operator==(const WifiConfig& other) const
{
	return network == other.network && passphrase == other.passphrase && address == other.address;
}

}  // namespace w2b
