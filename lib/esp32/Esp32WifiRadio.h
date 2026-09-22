#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <DNSServer.h>
#include <WiFi.h>

#include "FreeRtosMutex.h"
#include "WifiRadio.h"

namespace w2b
{

// WifiRadio over the Arduino WiFi library, with the DNS catch-all that makes the access point a captive portal; WiFi settings stay in
// RAM, the core's own reconnect is off. Transmits the open access point, joins the network, probes for its name while roaming.
class Esp32WifiRadio : public WifiRadio
{
public:
	// a roaming scan's time on each channel; the core's default is 300 ms
	static constexpr uint32_t kScanMsPerChannel = 120;

	// apSSID: the open access point's name
	explicit Esp32WifiRadio(const char* apSSID);

	// AP + STA with the DNS catch-all; the access point has no password
	void openPortal() override;

	// station only
	void closePortal() override;

	// clients on the access point
	uint8_t portalClients() const override;

	// joins the strongest access point with the name
	void connect(const WifiConfig& wifi, const char* hostname) override;

	// joins target's BSSID on its channel
	void connectTo(const WifiConfig& wifi, const char* hostname, const AccessPoint& target) override;

	// drops the link or the attempt at once
	void disconnect() override;

	// WL_CONNECTED
	bool linkUp() const override;

	// from the reason of the last station disconnect
	JoinFailure joinFailure() const override;

	// the station's signal from the driver, kept for link()
	int8_t linkRssi() override;

	// the station's access point
	BSSID linkBssid() const override;

	// a copy of the link's last facts, under the lock
	WifiLink link() const override;

	// An active, directed scan for ssid, kScanMsPerChannel per channel; refused while any scan runs, the setup page's included.
	bool startScan(const char* ssid) override;

	// the scan's access points with the scanned name only
	ScanStatus scanResult(std::vector<AccessPoint>& found) override;

	// address, mask, gateway, DNS
	std::string addressSummary() const override;

	// signal, channel, BSSID, hostname
	std::string radioSummary() const override;

private:
	// Settings of the WiFi library and the event listener, on first use: the WiFi object may not exist when globals are built.
	void prepare();

	// the driver's station events, on the WiFi event task: the join's facts, the addresses, the end of the link
	void handleEvent(arduino_event_id_t id, const arduino_event_info_t& info);

	bool configureStation(const WifiConfig& wifi, const char* hostname);

	// target null: the strongest access point with the name
	void join(const WifiConfig& wifi, const char* hostname, const AccessPoint* target);

	const char* apSSID;

	DNSServer dns;

	// prepare() registered the event listener; application thread only
	bool listening = false;

	// the name the running roaming scan looks for; the driver reads it while it scans
	WifiConfig::SSID scanSSID;

	// reason code of the last station disconnect since connect(), 0 for none; written by the WiFi event task
	std::atomic<uint16_t> lastReason{0};

	// the link's last facts; written by the WiFi event task and the thread that samples the signal, read by the web server
	WifiLink current;

	mutable FreeRtosMutex lock;
};

}  // namespace w2b
