#pragma once

#include <optional>
#include <string>

#include "FixedString.h"
#include "JsonSerializable.h"
#include "StaticIpConfig.h"

namespace w2b
{

// Station settings, "wifi" in /config.json. The default state is valid: not configured, DHCP.
class WifiConfig : public IJsonSerializable<WifiConfig>
{
public:
	// a network name, up to 32 bytes
	using SSID = FixedString<32>;

	// WPA2: 8..63 printable characters or 64 hex digits
	using Password = FixedString<64>;

	// not configured, DHCP
	WifiConfig() = default;

	// staticIp none: DHCP
	WifiConfig(const SSID& ssid, const Password& password, const std::optional<StaticIpConfig>& staticIp);

	// empty: not configured, the setup portal opens
	inline const SSID& ssid() const { return network; }

	// empty: an open network; stored in plain text
	inline const Password& password() const { return passphrase; }

	// none: DHCP
	inline const std::optional<StaticIpConfig>& staticIp() const { return address; }

	// The SSID fits; the password follows the WPA2 rules; the static address is valid if there is one.
	bool validate(std::string& error) const;

	// ssid, password, static (null for DHCP)
	void toJson(JsonObject& json) const override;

	// Keys the JSON leaves out keep their value; "static": null means DHCP, an object a complete static address.
	bool fromJson(const JsonObjectConst& json, std::string& error) override;

	// every field equal
	bool operator==(const WifiConfig& other) const;

private:
	bool readStaticIp(const JsonObjectConst& json, std::string& error);

	SSID network;

	Password passphrase;

	std::optional<StaticIpConfig> address;
};

}  // namespace w2b
