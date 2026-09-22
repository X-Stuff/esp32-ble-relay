#pragma once

#include "ConfigWebComponent.h"
#include "JsonForward.h"

namespace w2b
{

class RestartService;
class TimeSync;

// The configuration over HTTP: view, export, import, time zone, log level and heartbeat, battery scale, save state and the zone list;
// LAN address only. Writes /config.json on an import, a time zone, a device change or a battery calibration; restarts after an import.
class ConfigService : public ConfigWebComponent
{
public:
	// time: takes a saved time zone; restart: follows a saved import
	ConfigService(AsyncWebServer& server, Config& config, TimeSync& time, RestartService& restart);

private:
	void addRoutes() override;

	// GET /api/config: the running configuration without the WiFi password
	void handleView(AsyncWebServerRequest* request) const;

	// GET /api/config/export: the whole configuration, the WiFi password in plain text, as the download <hostname>-config.json
	void handleExport(AsyncWebServerRequest* request) const;

	// POST /api/config/import: "schema" required, keys left out keep their values, "rules" replaces every rule; then a restart
	void handleImport(AsyncWebServerRequest* request, JsonVariant& json);

	// PUT /api/config/timezone: {"timezone": "Europe/Berlin"}, in use once saved
	void handleTimeZone(AsyncWebServerRequest* request, JsonVariant& json);

	// GET /api/config/device: {log_level, heartbeat_s, battery_scale_ppm, battery_mv: the last reading, levels: every level's name,
	// limits: {heartbeat_s}}
	void handleDeviceGet(AsyncWebServerRequest* request) const;

	// PUT /api/config/device: {log_level, heartbeat_s}, a key left out keeps its value; hostname, timezone, use_random_ble_address and
	// battery_scale_ppm refused; in use once saved
	void handleDevicePut(AsyncWebServerRequest* request, JsonVariant& json);

	// PUT /api/config/battery: {"millivolts": N}, the battery's actual voltage: the scale that makes the relay's measured voltage N; 0
	// sets the scale back to none. In use from the next reading once saved
	void handleBattery(AsyncWebServerRequest* request, JsonVariant& json);

	// GET /api/config/save: {"save": "idle" | "pending" | "saved" | "failed"} of the last change
	void handleSaveState(AsyncWebServerRequest* request) const;

	// GET /api/timezones: {"zones": [...]}, every name of the table, sorted
	void handleTimeZones(AsyncWebServerRequest* request) const;

	TimeSync& time;

	RestartService& restart;
};

}  // namespace w2b
