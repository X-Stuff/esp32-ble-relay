#include "ConfigService.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "BatteryMonitor.h"
#include "ConfigStore.h"
#include "DeviceConfig.h"
#include "JsonMembers.h"
#include "Logger.h"
#include "RestartService.h"
#include "TimeSync.h"
#include "TimeZones.h"

namespace w2b
{

namespace
{

// {"timezone": "..."}: the longest zone name is 30 characters
constexpr size_t kMaxZoneBodyBytes = 256;

// {"log_level": "debug", "heartbeat_s": 3600}
constexpr size_t kMaxDeviceBodyBytes = 256;

// {"millivolts": 4200}
constexpr size_t kMaxBatteryBodyBytes = 64;

// Piece item of {"zones":["A","B"]} into out: the opening, each name quoted (a comma before all but the first), the closing; 0 past it.
size_t timeZoneItem(size_t item, char* out, size_t cap)
{
	const size_t count = timeZoneCount();

	if (item == 0)
	{
		return static_cast<size_t>(std::snprintf(out, cap, "{\"zones\":["));
	}

	if (item <= count)
	{
		return static_cast<size_t>(std::snprintf(out, cap, "%s\"%s\"", item == 1 ? "" : ",", timeZoneAt(item - 1).name()));
	}

	return item == count + 1 ? static_cast<size_t>(std::snprintf(out, cap, "]}")) : 0;
}

// Copies up to room bytes of the pieces from item and offset on into out, and moves both past what was copied.
size_t fillTimeZones(uint8_t* out, size_t room, size_t& item, size_t& offset)
{
	size_t written = 0;

	while (written < room)
	{
		// the longest name with its quotes and comma
		char text[48];
		const size_t length = timeZoneItem(item, text, sizeof(text));

		if (length == 0)
		{
			break;
		}

		const size_t take = std::min(room - written, length - offset);
		std::memcpy(out + written, text + offset, take);
		written += take;
		offset += take;

		if (offset == length)
		{
			++item;
			offset = 0;
		}
	}

	return written;
}

}  // namespace

ConfigService::ConfigService(AsyncWebServer& server, Config& config, TimeSync& time, RestartService& restart)
	: ConfigWebComponent(server, config), time(time), restart(restart)
{
}

void ConfigService::addRoutes()
{
	server().on(AsyncURIMatcher::exact("/api/config"), HTTP_GET,
		[this](AsyncWebServerRequest* request)
		{
			handleView(request);
		});

	server().on(AsyncURIMatcher::exact("/api/config/export"), HTTP_GET,
		[this](AsyncWebServerRequest* request)
		{
			handleExport(request);
		});

	server()
		.on(AsyncURIMatcher::exact("/api/config/import"), HTTP_POST,
			[this](AsyncWebServerRequest* request, JsonVariant& json)
			{
				handleImport(request, json);
			})
		.setMaxContentLength(ConfigStore::kMaxBytes);

	server()
		.on(AsyncURIMatcher::exact("/api/config/timezone"), HTTP_PUT,
			[this](AsyncWebServerRequest* request, JsonVariant& json)
			{
				handleTimeZone(request, json);
			})
		.setMaxContentLength(kMaxZoneBodyBytes);

	server().on(AsyncURIMatcher::exact("/api/config/device"), HTTP_GET,
		[this](AsyncWebServerRequest* request)
		{
			handleDeviceGet(request);
		});

	server()
		.on(AsyncURIMatcher::exact("/api/config/device"), HTTP_PUT,
			[this](AsyncWebServerRequest* request, JsonVariant& json)
			{
				handleDevicePut(request, json);
			})
		.setMaxContentLength(kMaxDeviceBodyBytes);

	server()
		.on(AsyncURIMatcher::exact("/api/config/battery"), HTTP_PUT,
			[this](AsyncWebServerRequest* request, JsonVariant& json)
			{
				handleBattery(request, json);
			})
		.setMaxContentLength(kMaxBatteryBodyBytes);

	server().on(AsyncURIMatcher::exact("/api/config/save"), HTTP_GET,
		[this](AsyncWebServerRequest* request)
		{
			handleSaveState(request);
		});

	server().on(AsyncURIMatcher::exact("/api/timezones"), HTTP_GET,
		[this](AsyncWebServerRequest* request)
		{
			handleTimeZones(request);
		});
}

void ConfigService::handleView(AsyncWebServerRequest* request) const
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();

	config().read([&root](const ConfigData& data)
		{
			data.toJson(root);
		});

	JsonObject wifi = root["wifi"];
	wifi.remove("password");

	sendJson(request, 200, std::move(doc));
}

void ConfigService::handleExport(AsyncWebServerRequest* request) const
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();

	config().read([&root](const ConfigData& data)
		{
			data.toJson(root);
		});

	const std::string disposition = "attachment; filename=\"" + std::string(config().device().hostname().c_str()) + "-config.json\"";
	const bool pretty = true;
	AsyncWebServerResponse* response = makeJsonResponse(200, std::move(doc), pretty);
	response->addHeader("Content-Disposition", disposition.c_str());
	request->send(response);
}

// The file merges into the staging copy of the running configuration, so a key it leaves out keeps its value.
void ConfigService::handleImport(AsyncWebServerRequest* request, JsonVariant& json)
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	if (!json.is<JsonObjectConst>())
	{
		sendError(request, 400, "not a JSON object");
		return;
	}

	const JsonObjectConst file = json.as<JsonObjectConst>();

	submit(request, "configuration import",
		[&file](ConfigData& newConfig, std::string& error)
		{
			return newConfig.fromJson(file, error);
		},
		[this]
		{
			restart.schedule("configuration imported");
		});
}

// The zone is in use before the page reads "saved", so its next status shows the new local time.
void ConfigService::handleTimeZone(AsyncWebServerRequest* request, JsonVariant& json)
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	if (!json.is<JsonObjectConst>() || !json["timezone"].is<const char*>())
	{
		sendError(request, 400, "timezone: expected a string");
		return;
	}

	const char* name = json["timezone"].as<const char*>();
	const TimeZone* zone = findTimeZone(name);

	if (zone == nullptr)
	{
		sendError(request, 400, "timezone: \"" + std::string(name) + "\" is not a known time zone name, e.g. \"Europe/Berlin\" or \"UTC\"");
		return;
	}

	char what[64];
	std::snprintf(what, sizeof(what), "time zone %s", zone->name());

	submit(request, what,
		[zone](ConfigData& newConfig, std::string& /*error*/)
		{
			const DeviceConfig& device = newConfig.device();
			newConfig.setDevice(
				DeviceConfig(device.hostname(), device.logLevel(), *zone, device.useRandomBleAddress(), device.heartbeatS(), device.batteryScalePpm()));
			return true;
		},
		[this, zone]
		{
			time.applyTimeZone(*zone);
		});
}

void ConfigService::handleDeviceGet(AsyncWebServerRequest* request) const
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	const DeviceConfig device = config().device();

	JsonDocument doc;
	doc["log_level"] = logLevelName(device.logLevel());
	doc["heartbeat_s"] = device.heartbeatS();
	doc["battery_scale_ppm"] = device.batteryScalePpm();
	doc["battery_mv"] = BatteryMonitor::millivolts();

	JsonArray levels = doc["levels"].to<JsonArray>();

	for (LogLevel level : LogLevels)
	{
		levels.add(logLevelName(level));
	}

	doc["limits"]["heartbeat_s"] = DeviceConfig::kMaxHeartbeatS;
	sendJson(request, 200, std::move(doc));
}

// The keys that need a restart or have their own call are refused; the rest merge into the device settings as an import would.
void ConfigService::handleDevicePut(AsyncWebServerRequest* request, JsonVariant& json)
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	if (!json.is<JsonObjectConst>())
	{
		sendError(request, 400, "not a JSON object");
		return;
	}

	const JsonObjectConst body = json.as<JsonObjectConst>();

	for (const char* key : {"hostname", "use_random_ble_address", "timezone", "battery_scale_ppm"})
	{
		if (!isMissing(body, key))
		{
			sendError(request, 400, std::string(key) + ": not settable here; an import changes it, the time zone and the battery scale also their own call");
			return;
		}
	}

	submit(request, "log level and heartbeat",
		[&body](ConfigData& newConfig, std::string& reason)
		{
			DeviceConfig device = newConfig.device();

			if (!device.fromJson(body, reason))
			{
				return false;
			}

			newConfig.setDevice(device);
			return true;
		},
		[this]
		{
			Logger::setLevel(config().device().logLevel());
		});
}

// The scale comes from the measured voltage without one, so a second calibration replaces the first instead of adding to it.
void ConfigService::handleBattery(AsyncWebServerRequest* request, JsonVariant& json)
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	if (!json.is<JsonObjectConst>())
	{
		sendError(request, 400, "not a JSON object");
		return;
	}

	uint32_t actual = 0;
	std::string error;

	if (!readUint(json.as<JsonObjectConst>(), "millivolts", actual, error))
	{
		sendError(request, 400, error);
		return;
	}

	uint32_t scalePpm = DeviceConfig::kUncalibratedBatteryScalePpm;

	if (actual != 0)
	{
		const uint32_t measured = BatteryMonitor::uncalibratedMillivolts();

		if (measured < BatteryMonitor::kNoBatteryBelowMv)
		{
			sendError(request, 409, "no battery: the relay measures " + std::to_string(measured) + " mV");
			return;
		}

		scalePpm = (uint32_t)(((uint64_t)actual * DeviceConfig::kUncalibratedBatteryScalePpm + measured / 2) / measured);
	}

	submit(request, "battery scale",
		[scalePpm](ConfigData& newConfig, std::string& /*error*/)
		{
			const DeviceConfig& device = newConfig.device();
			newConfig.setDevice(
				DeviceConfig(device.hostname(), device.logLevel(), device.timeZone(), device.useRandomBleAddress(), device.heartbeatS(), scalePpm));
			return true;
		},
		[scalePpm]
		{
			BatteryMonitor::setScale(scalePpm);
		});
}

void ConfigService::handleSaveState(AsyncWebServerRequest* request) const
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	sendSaveState(request, 200, config().saveState());
}

// Piece by piece from the table into the web server's send buffer; the names need no escaping, the table holds no quote or backslash.
void ConfigService::handleTimeZones(AsyncWebServerRequest* request) const
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	AsyncWebServerResponse* response = request->beginChunkedResponse("application/json",
		[item = size_t(0), offset = size_t(0)](uint8_t* buffer, size_t room, size_t /*index*/) mutable
		{
			return fillTimeZones(buffer, room, item, offset);
		});
	request->send(response);
}

}  // namespace w2b
