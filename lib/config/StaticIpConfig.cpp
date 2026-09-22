#include "StaticIpConfig.h"

#include "ConfigRules.h"
#include "IPv4Str.h"
#include "JsonMembers.h"

namespace w2b
{

namespace
{

bool readIPv4(const JsonObjectConst& json, const char* key, bool required, uint32_t& address, std::string& error)
{
	const char* text = "";

	if (required ? !readText(json, key, text, error) : !readOptionalText(json, key, text, error))
	{
		return false;
	}

	if (!required && *text == '\0')
	{
		address = 0;
		return true;
	}

	if (!parseIPv4(text, address))
	{
		error = std::string(key) + ": \"" + text + "\" is not an IPv4 address";
		return false;
	}

	return true;
}

}  // namespace

StaticIpConfig::StaticIpConfig(uint32_t ip, uint32_t gateway, uint32_t subnet, uint32_t dns)
	: address(ip), gatewayAddress(gateway), mask(subnet), dnsServer(dns)
{
}

bool StaticIpConfig::validate(std::string& error) const
{
	if (!isValidSubnetMask(mask))
	{
		error = "subnet: \"" + std::string(IPv4Str(mask).c_str()) + "\" is not a subnet mask";
		return false;
	}

	if ((address & mask) != (gatewayAddress & mask) || address == gatewayAddress)
	{
		error = "gateway: must be another address in the subnet of ip";
		return false;
	}

	return true;
}

void StaticIpConfig::toJson(JsonObject& json) const
{
	json["ip"] = IPv4Str(address).c_str();
	json["gateway"] = IPv4Str(gatewayAddress).c_str();
	json["subnet"] = IPv4Str(mask).c_str();
	json["dns"] = dnsServer != 0 ? IPv4Str(dnsServer).c_str() : "";
}

bool StaticIpConfig::fromJson(const JsonObjectConst& json, std::string& error)
{
	return readIPv4(json, "ip", true, address, error) && readIPv4(json, "gateway", true, gatewayAddress, error) &&
		readIPv4(json, "subnet", true, mask, error) && readIPv4(json, "dns", false, dnsServer, error) && validate(error);
}

bool StaticIpConfig::operator==(const StaticIpConfig& other) const
{
	return address == other.address && gatewayAddress == other.gatewayAddress && mask == other.mask && dnsServer == other.dnsServer;
}

}  // namespace w2b
