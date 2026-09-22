#pragma once

#include <cstdint>

#include "Bssid.h"

namespace w2b
{

// One access point of the saved network as a scan found it. WifiSupervisor's roaming compares them with the current link.
class AccessPoint
{
public:
	// bssid, rssi (dBm) and channel as the scan reported them
	AccessPoint(const BSSID& bssid, int8_t rssi, uint8_t channel);

	// the access point's MAC address
	inline const BSSID& bssid() const { return mac; }

	// dBm
	inline int8_t rssi() const { return signal; }

	// the radio channel
	inline uint8_t channel() const { return radioChannel; }

private:
	BSSID mac;

	int8_t signal;

	uint8_t radioChannel;
};

}  // namespace w2b
