#pragma once

#include <cstdint>

#include "ConfigWebComponent.h"
#include "JsonForward.h"
#include "WifiScanList.h"

namespace w2b
{

class Lock;
class WifiSupervisor;

// The setup page's API, on the setup access point and the LAN: the WiFi scan, and Connect, which saves the WiFi settings and hands them
// to the supervisor. Every unknown URL outside /api/ redirects to the page, so phones show it as a captive portal. Writes /config.json.
class SetupPortal : public ConfigWebComponent
{
public:
	// lock: guards the scan state; supervisor: told about every saved Connect
	SetupPortal(AsyncWebServer& server, Config& config, Lock& lock, WifiSupervisor& supervisor);

	// runs a requested scan
	void update(uint32_t dtMs) override;

private:
	// Where the scan for the page is
	enum class ScanState : uint8_t
	{
		// none asked for since boot
		Idle,

		// asked for by POST /api/wifi/scan; update() starts it
		Requested,

		// the driver scans
		Running,

		// the result is in scanList
		Done,

		// it did not start or ended without a result
		Failed,
	};

	static const char* nameOf(ScanState state);

	// every URL no route took: under /api/ a JSON error (answerUnknownApi), anything else a redirect to the page
	static void handleNotFound(AsyncWebServerRequest* request);

	void addRoutes() override;

	// GET /api/wifi: the saved settings without the password, "save" of the last change, "join" of the last Connect, "join_limit_s"
	void handleWifiGet(AsyncWebServerRequest* request);

	// PUT /api/wifi: Connect; the body replaces the whole wifi section, a key it leaves out takes its default; ssid required
	void handleWifiPut(AsyncWebServerRequest* request, JsonVariant& json);

	// POST /api/wifi/scan: asks for a scan; one requested or running is not restarted
	void handleScanStart(AsyncWebServerRequest* request);

	// GET /api/wifi/scan/results: {state, networks}
	void handleScanResults(AsyncWebServerRequest* request);

	void runScan();

	void startScan();

	void collectScan();

	void setScanState(ScanState state);

	Lock& lock;

	WifiSupervisor& supervisor;

	// handlers move it out of Idle, Done and Failed only, update() out of Requested and Running only
	ScanState scanState = ScanState::Idle;

	// the last completed scan
	WifiScanList scanList;
};

}  // namespace w2b
