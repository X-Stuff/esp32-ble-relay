#pragma once

#include "JsonForward.h"
#include "WebComponent.h"

namespace w2b
{

class BleConnectionManager;
class OtaService;
class TimeSync;
class WifiLink;
class WifiRadio;

// The relay's state for the web UI: the device, the WiFi link, the app slots, the clock and every relay rule's BLE connection.
class StatusService : public WebComponent
{
public:
	// ota: the app slots; time: the local time and zone; relays: the rules' connections and the relay's own address; radio: the WiFi
	// link's last facts. firmware, built: shown as they are; must stay valid for the whole run
	StatusService(AsyncWebServer& server, const OtaService& ota, const TimeSync& time, const BleConnectionManager& relays, const WifiRadio& radio,
		const char* firmware, const char* built);

private:
	void addRoutes() override;

	// GET /api/status, polled by the web UI: {device, wifi, ota, time, relays}, read from the radio's last link facts, the heap, the
	// clock and the modules; relays one per rule, in the configuration's order
	void handleStatus(AsyncWebServerRequest* request) const;

	// the device object: the link's hostname, firmware, build, chip, uptime, heap and the relay's BLE address
	void writeDevice(JsonObject& device, const WifiLink& link) const;

	// the wifi object: the link's facts and the station's MAC
	static void writeWifi(JsonObject& wifi, const WifiLink& link);

private:
	const OtaService& ota;

	const TimeSync& time;

	const BleConnectionManager& relays;

	const WifiRadio& radio;

	const char* firmware;

	const char* built;
};

}  // namespace w2b
