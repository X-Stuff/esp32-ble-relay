#include "BleService.h"

#include <optional>
#include <string>
#include <utility>

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <NimBLEDevice.h>

#include "BleDeviceBase.h"
#include "BleDiscovery.h"
#include "BleScanner.h"
#include "Config.h"
#include "JsonMembers.h"
#include "Logger.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "ble";

constexpr const char* kScanPath = "/api/ble/scan";

constexpr const char* kResultsPath = "/api/ble/scan/results";

constexpr const char* kClearPath = "/api/ble/scan/clear";

constexpr const char* kDiscoverPath = "/api/ble/discover";

constexpr const char* kDiscoverResultPath = "/api/ble/discover/result";

// the handler holds a body on the heap before parsing it; a longer one gets 413. A probe request with some whitespace fits
constexpr size_t kMaxBodyBytes = 256;

// a connect while NimBLE scans ends the scan
bool scanRuns(BleScanner::State state)
{
	return state == BleScanner::State::Requested || state == BleScanner::State::Running || state == BleScanner::State::Ended;
}

bool probeRuns(BleDiscovery::State state)
{
	return state != BleDiscovery::State::Idle && state != BleDiscovery::State::Done && state != BleDiscovery::State::Failed;
}

// The body's "address" and "address_type", both required, and "connectable", true when missing; or false with the error.
bool readProbeRequest(JsonVariant& json, BleAddress& address, BleDeviceBase::AddressType& type, bool& connectable, std::string& error)
{
	const char* text = nullptr;

	if (!json.is<JsonObjectConst>())
	{
		error = "not a JSON object";
		return false;
	}

	const JsonObjectConst body = json.as<JsonObjectConst>();

	if (!readText(body, "address", text, error))
	{
		return false;
	}

	if (!BleAddress::parse(text, address))
	{
		error = "address: \"" + std::string(text) + "\" is not a BLE address, e.g. \"12:34:56:78:9a:bc\"";
		return false;
	}

	if (isMissing(body, "address_type"))
	{
		error = "address_type: missing";
		return false;
	}

	connectable = true;

	return readOptionalChoice(body, "address_type", type, BleDeviceBase::kAddressTypes, error) && readOptionalBool(body, "connectable", connectable, error);
}

}  // namespace

BleService::BleService(AsyncWebServer& server, const Config& config, BleScanner& scanner, BleDiscovery& discovery)
	: WebComponent(server), config(config), scanner(scanner), discovery(discovery)
{
}

// A hostname beyond NimBLE's 31-byte GAP name leaves its default name, "nimble". Without NimBLE every scan and probe fails.
void BleService::setup()
{
	const DeviceConfig device = config.device();

	if (NimBLEDevice::init(device.hostname().c_str()))
	{
		// NimBLE creates a random static address at each start; public, the chip's own, is its default
		if (device.useRandomBleAddress() && !NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM))
		{
			Logger::warn(kTag, "NimBLE has no random address; the public one is in use");
		}

		const NimBLEAddress own = NimBLEDevice::getAddress();
		Logger::info(kTag, "NimBLE running as central and observer, %s address %s, up to %u connections",
			own.getType() == BLE_ADDR_RANDOM ? "random" : "public", own.toString().c_str(), (unsigned)CONFIG_BT_NIMBLE_MAX_CONNECTIONS);
	}
	else
	{
		Logger::error(kTag, "starting NimBLE failed; no BLE scan, probe or relay runs");
	}

	WebComponent::setup();
}

void BleService::addRoutes()
{
	server()
		.on(AsyncURIMatcher::exact(kScanPath), HTTP_POST,
			[this](AsyncWebServerRequest* request, JsonVariant& json)
			{
				handleScanStart(request, json);
			})
		.setMaxContentLength(kMaxBodyBytes);

	server().on(AsyncURIMatcher::exact(kResultsPath), HTTP_GET,
		[this](AsyncWebServerRequest* request)
		{
			handleScanResults(request);
		});

	server().on(AsyncURIMatcher::exact(kClearPath), HTTP_POST,
		[this](AsyncWebServerRequest* request)
		{
			handleScanClear(request);
		});

	server()
		.on(AsyncURIMatcher::exact(kDiscoverPath), HTTP_POST,
			[this](AsyncWebServerRequest* request, JsonVariant& json)
			{
				handleDiscoverStart(request, json);
			})
		.setMaxContentLength(kMaxBodyBytes);

	server().on(AsyncURIMatcher::exact(kDiscoverResultPath), HTTP_GET,
		[this](AsyncWebServerRequest* request)
		{
			handleDiscoverResult(request);
		});
}

void BleService::handleScanStart(AsyncWebServerRequest* request, JsonVariant& json)
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	uint32_t seconds = BleScanner::kDefaultSeconds;
	std::string error;

	if (!json.is<JsonObjectConst>())
	{
		sendError(request, 400, "not a JSON object");
		return;
	}

	if (!readOptionalUint(json.as<JsonObjectConst>(), "seconds", seconds, error))
	{
		sendError(request, 400, error);
		return;
	}

	if (seconds < 1 || seconds > BleScanner::kMaxSeconds)
	{
		sendError(request, 400, "seconds: " + std::to_string(seconds) + " is out of range 1-" + std::to_string(BleScanner::kMaxSeconds));
		return;
	}

	if (probeRuns(discovery.currentState()))
	{
		sendError(request, 409, "a BLE probe runs; scan once it ended");
		return;
	}

	JsonDocument doc;
	doc["state"] = BleScanner::stateStr(scanner.request(seconds));
	doc["seconds"] = scanner.seconds();

	sendJson(request, 202, std::move(doc));
}

void BleService::handleScanResults(AsyncWebServerRequest* request) const
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	BleScanList devices;
	const BleScanner::State state = scanner.read(devices);

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	root["state"] = BleScanner::stateStr(state);
	devices.toJson(root);

	sendJson(request, 200, std::move(doc));
}

void BleService::handleScanClear(AsyncWebServerRequest* request)
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	if (!scanner.clearDevices())
	{
		sendError(request, 409, "a BLE scan runs; clear once it ended");
		return;
	}

	JsonDocument doc;
	doc["state"] = BleScanner::stateStr(scanner.currentState());

	sendJson(request, 200, std::move(doc));
}

// The checks and the request run on the web server's one task, so no other request comes between them.
void BleService::handleDiscoverStart(AsyncWebServerRequest* request, JsonVariant& json)
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	BleAddress address;
	BleDeviceBase::AddressType type = BleDeviceBase::AddressType::Public;
	bool connectable = true;
	std::string error;

	if (!readProbeRequest(json, address, type, connectable, error))
	{
		sendError(request, 400, error);
		return;
	}

	if (!connectable)
	{
		sendError(request, 400, "connectable: false; the device's advertisements accept no connection, so it cannot be probed");
		return;
	}

	if (scanRuns(scanner.currentState()))
	{
		sendError(request, 409, "a BLE scan runs; probe once it ended");
		return;
	}

	if (!discovery.request(address, type))
	{
		sendError(request, 409, "a BLE probe runs; one at a time");
		return;
	}

	JsonDocument doc;
	doc["state"] = BleDiscovery::stateStr(discovery.currentState());

	sendJson(request, 202, std::move(doc));
}

void BleService::handleDiscoverResult(AsyncWebServerRequest* request) const
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	BleAddress address;
	GattTable table;
	const BleDiscovery::State state = discovery.read(address, table);

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	root["state"] = BleDiscovery::stateStr(state);
	root["address"] = state == BleDiscovery::State::Idle ? "" : address.text().c_str();
	table.toJson(root);

	sendJson(request, 200, std::move(doc));
}

}  // namespace w2b
