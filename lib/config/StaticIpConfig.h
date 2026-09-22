#pragma once

#include <cstdint>
#include <string>

#include "JsonSerializable.h"

namespace w2b
{

// The station's static IPv4 settings, "wifi.static" in /config.json; addresses in host order, 192.168.0.1 is 0xc0a80001. Has no valid
// default: the empty object exists only for fromJson.
class StaticIpConfig : public IJsonSerializable<StaticIpConfig>
{
public:
	// dns 0: none
	StaticIpConfig(uint32_t ip, uint32_t gateway, uint32_t subnet, uint32_t dns);

private:
	friend class SerializableFactory;

	StaticIpConfig() = default;

public:
	// the station's address
	inline uint32_t ip() const { return address; }

	// the router
	inline uint32_t gateway() const { return gatewayAddress; }

	// the subnet mask
	inline uint32_t subnet() const { return mask; }

	// the DNS server; 0: none
	inline uint32_t dns() const { return dnsServer; }

	// A contiguous mask; the gateway another address in the subnet of ip.
	bool validate(std::string& error) const;

	// ip, gateway, subnet as dotted quads; dns "" for none
	void toJson(JsonObject& json) const override;

	// ip, gateway and subnet required; dns optional, "" for none
	bool fromJson(const JsonObjectConst& json, std::string& error) override;

	// every field equal
	bool operator==(const StaticIpConfig& other) const;

private:
	uint32_t address = 0;

	uint32_t gatewayAddress = 0;

	uint32_t mask = 0;

	uint32_t dnsServer = 0;
};

}  // namespace w2b
