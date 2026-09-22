#include "RestartService.h"

#include <utility>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "Logger.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "restart";

}  // namespace

RestartService::RestartService(AsyncWebServer& server)
	: WebComponent(server)
{
}

void RestartService::update(uint32_t dtMs)
{
	const char* reason = requested;

	if (reason == nullptr)
	{
		return;
	}

	waitedMs += dtMs;

	if (waitedMs >= kDelayMs)
	{
		Logger::info(kTag, "restarting: %s", reason);
		Serial.flush();
		ESP.restart();
	}
}

void RestartService::schedule(const char* reason)
{
	const char* none = nullptr;
	requested.compare_exchange_strong(none, reason != nullptr ? reason : "no reason given");
}

void RestartService::addRoutes()
{
	server().on(AsyncURIMatcher::exact("/api/reboot"), HTTP_POST,
		[this](AsyncWebServerRequest* request)
		{
			handleReboot(request);
		});
}

void RestartService::handleReboot(AsyncWebServerRequest* request)
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	schedule("Reboot reason: HTTP api request");

	JsonDocument doc;
	doc["restart_in_ms"] = kDelayMs;
	sendJson(request, 202, std::move(doc));
}

}  // namespace w2b
