#pragma once

#include <cstdint>

#include "AccessPoint.h"
#include "DeviceConfig.h"
#include "JsonForward.h"
#include "WifiConfig.h"

namespace w2b
{

// The station's link as the driver last reported it, for the status page: the join's facts and the addresses from the driver's
// events, the signal from the last sample. Addresses in host order, 192.168.0.1 is 0xc0a80001.
class WifiLink
{
public:
	// no link
	WifiLink() = default;

	// associated and holding an address
	inline bool up() const { return connected; }

	// the network's name; empty while down
	inline const WifiConfig::SSID& ssid() const { return network; }

	// the access point of the link
	inline const BSSID& bssid() const { return accessPoint; }

	// the access point's channel; 0 while down
	inline uint8_t channel() const { return channelNumber; }

	// the last signal sample in dBm; 0 while down
	inline int8_t rssi() const { return signal; }

	inline uint32_t address() const { return ip; }

	inline uint32_t mask() const { return netmask; }

	inline uint32_t gateway() const { return gw; }

	inline uint32_t dns() const { return nameServer; }

	// the name the station joins as
	inline const DeviceConfig::Hostname& hostname() const { return name; }

	// the station associated: the network's name, its access point and channel
	void setNetwork(const char* ssid, const BSSID& bssid, uint8_t channel);

	// the station holds an address: up from now
	void setAddresses(uint32_t address, uint32_t mask, uint32_t gateway, uint32_t dns);

	// the latest signal sample, in dBm
	void setRssi(int8_t rssi);

	// the name the station joins as
	void setHostname(const char* hostname);

	// the link ended: down, its network, addresses and signal cleared; the hostname stays
	void clear();

	// "ssid", "connected", "address", "mask", "gateway", "dns", "rssi", "channel", "bssid"
	void toJson(JsonObject& json) const;

private:
	bool connected = false;

	WifiConfig::SSID network;

	BSSID accessPoint{};

	uint8_t channelNumber = 0;

	int8_t signal = 0;

	uint32_t ip = 0;

	uint32_t netmask = 0;

	uint32_t gw = 0;

	uint32_t nameServer = 0;

	DeviceConfig::Hostname name;
};

}  // namespace w2b
