#include "StatusService.h"

#include <optional>
#include <utility>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <esp_timer.h>
#include <profiler.h>

#include "BatteryMonitor.h"
#include "BleAddress.h"
#include "BleConnectionManager.h"
#include "BleDeviceBase.h"
#include "BoardInfo.h"
#include "JsonMembers.h"
#include "OtaService.h"
#include "TimeSync.h"
#include "WifiLink.h"
#include "WifiRadio.h"

namespace w2b
{

StatusService::StatusService(AsyncWebServer& server, const OtaService& ota, const TimeSync& time, const BleConnectionManager& relays,
	const WifiRadio& radio, const char* firmware, const char* built)
	: WebComponent(server), ota(ota), time(time), relays(relays), radio(radio), firmware(firmware), built(built)
{
}

void StatusService::addRoutes()
{
	server()
		.on(AsyncURIMatcher::exact("/api/status"), HTTP_GET,
			[this](AsyncWebServerRequest* request)
			{
				handleStatus(request);
			});
}

void StatusService::handleStatus(AsyncWebServerRequest* request) const
{
	JsonDocument doc;
	const WifiLink link = radio.link();
	JsonObject device = doc["device"].to<JsonObject>();
	writeDevice(device, link);

	JsonObject wifi = doc["wifi"].to<JsonObject>();
	writeWifi(wifi, link);

	JsonObject slots = doc["ota"].to<JsonObject>();
	ota.toJson(slots);

	JsonObject clock = doc["time"].to<JsonObject>();
	time.toJson(clock);

	JsonArray list = doc["relays"].to<JsonArray>();
	relays.readStatuses([&list](const std::vector<RelayStatus>& statuses)
		{
			for (const RelayStatus& relay : statuses)
			{
				JsonObject item = list.add<JsonObject>();
				relay.toJson(item);
			}
		});

	sendJson(request, 200, std::move(doc));
}

void StatusService::writeDevice(JsonObject& device, const WifiLink& link) const
{
	device["hostname"] = link.hostname().c_str();
	device["firmware"] = firmware;
	device["built"] = built;
	device["chip"] = BoardInfo::chipName().c_str();

	// esp_timer counts microseconds in 64 bits: no wrap, unlike millis() after 49 days
	device["uptime_s"] = esp_timer_get_time() / 1000000;

	device["heap_free"] = ESP.getFreeHeap();
	device["heap_min_free"] = ESP.getMinFreeHeap();
	device["heap_largest"] = ESP.getMaxAllocHeap();
	device["battery_mv"] = BatteryMonitor::millivolts();

	// the address BLE connects and scans use; "" while NimBLE does not run
	const std::optional<BleAddress>& own = relays.ownAddress();
	device["ble_address"] = own ? own->text().c_str() : "";
	device["ble_address_type"] = own ? choiceName(relays.ownAddressType(), BleDeviceBase::kAddressTypes) : "";
}

void StatusService::writeWifi(JsonObject& wifi, const WifiLink& link)
{
	link.toJson(wifi);
	wifi["mac"] = BoardInfo::wifiMac().c_str();
}

}  // namespace w2b
