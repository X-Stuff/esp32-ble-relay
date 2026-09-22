#pragma once

#include "JsonForward.h"
#include "WebComponent.h"

namespace w2b
{

class BleDiscovery;
class BleScanner;
class Config;

// NimBLE as central and observer; the BLE scan and the probe of one device over HTTP, for the page, taking turns; LAN address
// only. A scan sends scan requests to the devices in range, a probe connects to one and reads its GATT table.
class BleService : public WebComponent
{
public:
	// config: holds the hostname, NimBLE's GAP device name, which a connected device can read; scanner, discovery: run the scans, probes
	BleService(AsyncWebServer& server, const Config& config, BleScanner& scanner, BleDiscovery& discovery);

	// Starts NimBLE, host and controller, and waits until they are in sync; then adds the routes. Before every other BLE module's setup.
	void setup() override;

private:
	void addRoutes() override;

	// POST /api/ble/scan {"seconds": n}: 202 {state, seconds}; n 1 to BleScanner::kMaxSeconds, kDefaultSeconds without the key; one
	// requested or running is not restarted, and seconds is its length; 409 while a probe runs
	void handleScanStart(AsyncWebServerRequest* request, JsonVariant& json);

	// GET /api/ble/scan/results: {state, devices}, the devices found so far while it runs
	void handleScanResults(AsyncWebServerRequest* request) const;

	// POST /api/ble/scan/clear: 200 {state}, the relay's copy of the devices freed and the scan idle; 409 while a scan runs.
	// Frees memory only.
	void handleScanClear(AsyncWebServerRequest* request);

	// POST /api/ble/discover {"address", "address_type", "connectable"} as a scan listed the device: 202 {state}; 400 when not
	// connectable; 409 while a scan or a probe runs
	void handleDiscoverStart(AsyncWebServerRequest* request, JsonVariant& json);

	// GET /api/ble/discover/result: {state, address, services}, the services once done; address "" before the first probe
	void handleDiscoverResult(AsyncWebServerRequest* request) const;

	const Config& config;

	BleScanner& scanner;

	BleDiscovery& discovery;
};

}  // namespace w2b
