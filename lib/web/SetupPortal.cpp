#include "SetupPortal.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "ConfigRules.h"
#include "IPv4Str.h"
#include "Lock.h"
#include "Logger.h"
#include "WifiSupervisor.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "portal";

// a WiFi settings request is about 200 bytes
constexpr size_t kMaxBodyBytes = 1024;

// The address the request came in on is the page's, on the access point (192.168.4.1) and on the LAN.
void redirectToPage(AsyncWebServerRequest* request)
{
	request->redirect(String("http://") + request->client()->localIP().toString() + "/");
}

}  // namespace

SetupPortal::SetupPortal(AsyncWebServer& server, Config& config, Lock& lock, WifiSupervisor& supervisor)
	: ConfigWebComponent(server, config), lock(lock), supervisor(supervisor)
{
}

void SetupPortal::update(uint32_t /*dtMs*/)
{
	runScan();
}

const char* SetupPortal::nameOf(ScanState state)
{
	switch (state)
	{
		case ScanState::Idle:      return "idle";
		case ScanState::Requested: return "requested";
		case ScanState::Running:   return "running";
		case ScanState::Done:      return "done";
		case ScanState::Failed:    return "failed";
	}

	return "?";
}

void SetupPortal::addRoutes()
{
	server()
		.on(AsyncURIMatcher::exact("/api/wifi"), HTTP_GET,
			[this](AsyncWebServerRequest* request)
			{
				handleWifiGet(request);
			});

	server()
		.on(AsyncURIMatcher::exact("/api/wifi"), HTTP_PUT,
			[this](AsyncWebServerRequest* request, JsonVariant& json)
			{
				handleWifiPut(request, json);
			})
		.setMaxContentLength(kMaxBodyBytes);

	server()
		.on(AsyncURIMatcher::exact("/api/wifi/scan"), HTTP_POST,
			[this](AsyncWebServerRequest* request)
			{
				handleScanStart(request);
			});

	server()
		.on(AsyncURIMatcher::exact("/api/wifi/scan/results"), HTTP_GET,
			[this](AsyncWebServerRequest* request)
			{
				handleScanResults(request);
			});

	server().onNotFound(handleNotFound);
}

void SetupPortal::handleNotFound(AsyncWebServerRequest* request)
{
	if (!answerUnknownApi(request))
	{
		redirectToPage(request);
	}
}

// Without the password: anyone in range may join the open access point.
void SetupPortal::handleWifiGet(AsyncWebServerRequest* request)
{
	const WifiConfig wifi = config().wifi();

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	wifi.toJson(root);
	root.remove("password");
	root["save"] = Config::nameOf(config().saveState());
	root["join"] = WifiSupervisor::nameOf(supervisor.lastJoin());
	root["join_limit_s"] = WifiSupervisor::kJoinLimitMs / 1000;

	sendJson(request, 200, std::move(doc));
}

void SetupPortal::handleWifiPut(AsyncWebServerRequest* request, JsonVariant& json)
{
	WifiConfig wifi;
	std::string error;

	if (!json.is<JsonObjectConst>())
	{
		sendError(request, 400, "not a JSON object");
		return;
	}

	if (!wifi.fromJson(json.as<JsonObjectConst>(), error))
	{
		sendError(request, 400, error);
		return;
	}

	if (wifi.ssid().empty())
	{
		sendError(request, 400, "ssid: missing");
		return;
	}

	const std::string addressing = wifi.staticIp() ? "static address " + std::string(IPv4Str(wifi.staticIp()->ip()).c_str()) : "DHCP";
	Logger::info(kTag, "Connect: ssid \"%s\", password %u characters, %s", wifi.ssid().c_str(), (unsigned)wifi.password().size(), addressing.c_str());

	char what[64];
	std::snprintf(what, sizeof(what), "WiFi settings for \"%s\"", wifi.ssid().c_str());

	submit(request, what,
		[&wifi](ConfigData& newConfig, std::string& /*error*/)
		{
			newConfig.setWifi(wifi);
			return true;
		},
		[this]
		{
			supervisor.reconnect();
		});
}

void SetupPortal::handleScanStart(AsyncWebServerRequest* request)
{
	ScanState state = ScanState::Idle;

	{
		LockGuard guard(&lock);

		if (scanState != ScanState::Requested && scanState != ScanState::Running)
		{
			scanState = ScanState::Requested;
		}

		state = scanState;
	}

	JsonDocument doc;
	doc["state"] = nameOf(state);
	sendJson(request, 202, std::move(doc));
}

void SetupPortal::handleScanResults(AsyncWebServerRequest* request)
{
	WifiScanList list;
	ScanState state = ScanState::Idle;

	{
		LockGuard guard(&lock);
		list = scanList;
		state = scanState;
	}

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	root["state"] = nameOf(state);
	list.toJson(root);

	sendJson(request, 200, std::move(doc));
}

void SetupPortal::runScan()
{
	ScanState state = ScanState::Idle;

	{
		LockGuard guard(&lock);
		state = scanState;
	}

	if (state == ScanState::Requested)
	{
		startScan();
	}
	else if (state == ScanState::Running)
	{
		collectScan();
	}
}

// Active, asynchronous, hidden networks left out; a roaming scan holding the driver makes the request wait until it has ended.
void SetupPortal::startScan()
{
	if (WiFi.scanComplete() == WIFI_SCAN_RUNNING)
	{
		return;
	}

	const bool async = true;
	const bool showHidden = false;
	const bool started = WiFi.scanNetworks(async, showHidden) == WIFI_SCAN_RUNNING;

	setScanState(started ? ScanState::Running : ScanState::Failed);

	if (!started)
	{
		Logger::warn(kTag, "the WiFi scan did not start");
	}
}

void SetupPortal::collectScan()
{
	const int16_t count = WiFi.scanComplete();

	if (count == WIFI_SCAN_RUNNING)
	{
		return;
	}

	if (count < 0)
	{
		setScanState(ScanState::Failed);
		Logger::warn(kTag, "the WiFi scan failed or timed out");
		return;
	}

	WifiScanList list;

	for (int16_t i = 0; i < count; ++i)
	{
		list.add(WifiNetwork(WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN));
	}

	WiFi.scanDelete();

	{
		LockGuard guard(&lock);
		scanList = list;
		scanState = ScanState::Done;
	}

	Logger::info(kTag, "WiFi scan: %d access points, %u networks listed", count, (unsigned)list.networks().size());
}

void SetupPortal::setScanState(ScanState state)
{
	LockGuard guard(&lock);
	scanState = state;
}

}  // namespace w2b
