#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "JsonForward.h"
#include "WifiConfig.h"

namespace w2b
{

// One access point seen by a scan
class WifiNetwork
{
public:
	// rssi in dBm; secured: the network needs a password
	WifiNetwork(const WifiConfig::SSID& ssid, int32_t rssi, int32_t channel, bool secured);

	// the network name; empty for a hidden network
	inline const WifiConfig::SSID& ssid() const { return name; }

	// dBm
	inline int32_t rssi() const { return signal; }

	// the radio channel
	inline int32_t channel() const { return radioChannel; }

	// false: an open network, no password
	inline bool secured() const { return encrypted; }

	// "ssid", "rssi", "channel", "secured"
	void toJson(JsonObject& json) const;

private:
	WifiConfig::SSID name;

	int32_t signal;

	int32_t radioChannel;

	bool encrypted;
};

// WiFi scan results as the setup page lists them: one entry per SSID, the strongest access point of a mesh or repeater standing for
// all of them. Not persisted: toJson only, for the HTTP API.
class WifiScanList
{
public:
	// the most entries the list keeps
	static constexpr size_t kMaxNetworks = 20;

	// strongest first
	inline const std::vector<WifiNetwork>& networks() const { return found; }

	// Hidden: ignored. Known SSID: kept once, with the stronger signal. A full list drops its weakest entry.
	void add(const WifiNetwork& network);

	// "networks": [...]
	void toJson(JsonObject& json) const;

private:
	std::vector<WifiNetwork> found;
};

}  // namespace w2b
