// Firmware entry point. Formats an empty data partition, writes /config.json and OTA images, runs the open setup AP, takes NTP time, scans and probes BLE.
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <esp_bt.h>

#include "BuildConfig.h"

#include "BatteryMonitor.h"
#include "BleConnectionManager.h"
#include "BleDiscovery.h"
#include "BleScanner.h"
#include "BleService.h"
#include "BoardInfo.h"
#include "Config.h"
#include "ConfigService.h"
#include "ConfigStore.h"
#include "Esp32DigitalOut.h"
#include "Esp32WifiRadio.h"
#include "FreeRtosMutex.h"
#include "Heartbeat.h"
#include "IdfLogBridge.h"
#include "LedIndicator.h"
#include "LittleFsStorage.h"
#include "LogService.h"
#include "Logger.h"
#include "MdnsService.h"
#include "OtaService.h"
#include "RestartService.h"
#include "RuleService.h"
#include "Scheduler.h"
#include "SetupPortal.h"
#include "StatusService.h"
#include "TimeSync.h"
#include "TimeZones.h"
#include "WebPages.h"
#include "WifiSupervisor.h"

static_assert(CONFIG_BT_NIMBLE_MAX_CONNECTIONS >= 2 && CONFIG_BT_NIMBLE_MAX_CONNECTIONS <= BTDM_CONTROLLER_BLE_MAX_CONN_LIMIT,
	"CONFIG_BT_NIMBLE_MAX_CONNECTIONS: one for discovery, at least one for the rules, at most the ESP32 controller's limit");

namespace
{

void serialSink(const char* line, size_t len)
{
	Serial.write(reinterpret_cast<const uint8_t*>(line), len);
}

// The DEFAULT_* build values; an unknown log level falls back to info, an unknown zone to UTC.
w2b::DeviceConfig defaultDevice()
{
	w2b::LogLevel level = w2b::LogLevel::Info;
	w2b::parseLogLevel(DEFAULT_LOG_LEVEL, level);

	const w2b::TimeZone* zone = w2b::findTimeZone(DEFAULT_TIMEZONE);

	return w2b::DeviceConfig(DEFAULT_HOSTNAME, level, zone != nullptr ? *zone : w2b::defaultTimeZone(), DEFAULT_USE_RANDOM_BLE_ADDRESS,
		w2b::DeviceConfig::kDefaultHeartbeatS, w2b::DeviceConfig::kUncalibratedBatteryScalePpm);
}

char logStorage[LOG_RING_BYTES];
w2b::LogRing logRing(logStorage, sizeof(logStorage));
w2b::FreeRtosMutex logMutex;
w2b::Logger logger(logRing, w2b::TimeSync::logStamp, serialSink, &logMutex);

w2b::LittleFsStorage storage(FS_PARTITION_LABEL);
w2b::ConfigStore configStore(storage);

bool saveConfig(const w2b::ConfigData& data)
{
	return configStore.save(data);
}

w2b::FreeRtosMutex configMutex;
w2b::Config config(configMutex, saveConfig);

w2b::Esp32DigitalOut ledPin(LED_GPIO);
w2b::LedIndicator led(ledPin, LED_ACTIVE_LOW != 0);

w2b::BatteryMonitor battery(config);

w2b::Esp32WifiRadio wifiRadio(PORTAL_AP_SSID);
w2b::WifiSupervisor wifi(wifiRadio, config, led);
w2b::MdnsService mdns(config, HTTP_PORT);
w2b::TimeSync timeSync(NTP_SERVER_1, NTP_SERVER_2);

AsyncWebServer webServer(HTTP_PORT);
w2b::WebPages pages(webServer);
w2b::FreeRtosMutex portalMutex;
w2b::SetupPortal portal(webServer, config, portalMutex, wifi);
w2b::OtaService ota(webServer);
w2b::BleScanner bleScanner;
w2b::BleConnectionManager bleConnectionManager(config, bleScanner);
w2b::StatusService status(webServer, ota, timeSync, bleConnectionManager, wifiRadio, FW_VERSION, __DATE__ " " __TIME__);
w2b::LogService logService(webServer);
w2b::RestartService restart(webServer);
w2b::ConfigService configService(webServer, config, timeSync, restart);
w2b::RuleService ruleService(webServer, config);

w2b::BleDiscovery bleDiscovery(bleScanner);
w2b::BleService bleService(webServer, config, bleScanner, bleDiscovery);

//
// Schedulers (RtOS threads)
//
w2b::Scheduler relay(w2b::Thread::Settings
	{
		.name = "relay",
		.stackBytes = RELAY_STACK_BYTES,
		.priority = RELAY_PRIORITY,
		.core = RELAY_CORE
	});

	w2b::Scheduler application(w2b::Thread::Settings
	{
		.name = "application",
		.stackBytes = APPLICATION_STACK_BYTES,
		.priority = APPLICATION_PRIORITY,
		.core = APPLICATION_CORE
	});

w2b::Heartbeat heartbeat(config, logRing, application, relay);

// Without a mounted partition the defaults run and nothing is saved.
void loadConfig()
{
	const w2b::DeviceConfig defaults = defaultDevice();
	const w2b::LittleFsStorage::MountResult mount = storage.mount();

	if (mount == w2b::LittleFsStorage::MountResult::Failed)
	{
		w2b::Logger::error("storage", "mounting LittleFS on partition \"%s\" failed; defaults in use, nothing is saved", FS_PARTITION_LABEL);
		config.load([&defaults](w2b::ConfigData& data)
			{
				data.reset(defaults);
			});
		return;
	}

	w2b::Logger::info("storage", "LittleFS on partition \"%s\" %s, %u KB used of %u KB", FS_PARTITION_LABEL,
		mount == w2b::LittleFsStorage::MountResult::Formatted ? "formatted (first boot) and mounted" : "mounted",
		(unsigned)(storage.usedBytes() / 1024), (unsigned)(storage.totalBytes() / 1024));

	config.load([&defaults](w2b::ConfigData& data)
		{
			configStore.load(defaults, data);
		});
}

// Runs on the scheduler's own thread, once, at its first wake-up.
void logThreadStart(const char* name)
{
	w2b::Logger::info(name, "running on core %d, stack headroom %u B", xPortGetCoreID(), (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}

}  // namespace

void setup()
{
	Serial.setTxBufferSize(SERIAL_TX_BUFFER_BYTES);
	Serial.begin(SERIAL_BAUD);
	// let the monitor attach after the auto-reset
	delay(500);

	// Everything is visible until the config is loaded; then its log level applies.
	w2b::Logger::setLevel(w2b::LogLevel::Debug);
	w2b::Logger::info("boot", "w2b ble-relay fw %s, built %s %s", FW_VERSION, __DATE__, __TIME__);
	w2b::installIdfLogBridge(logMutex);
	w2b::BoardInfo::logBoot();

	loadConfig();
	w2b::Logger::setLevel(config.device().logLevel());

	// Before the schedulers, so every later line carries local time.
	timeSync.applyTimeZone(config.device().timeZone());

	// The transport: its modules are set up by the application thread, which starts it once NimBLE runs
	relay
		.once(0, []
			{
				logThreadStart("relay");
			})
		.every(RELAY_UPDATE_MS, [](uint32_t dtMs)
			{
				bleScanner.update(dtMs);
			})
		.every(RELAY_UPDATE_MS, [](uint32_t dtMs)
			{
				bleConnectionManager.update(dtMs);
			});

	// Everything else; started last, so no task is added while its thread runs
	application
		.once(0, []
			{
				logThreadStart("application");

				// the pattern follows the WiFi state, set by the supervisor
				led.setup();
				heartbeat.setup();
				battery.setup();

				// WiFi first: its start brings up the network stack that mDNS, SNTP and the web server need
				wifi.setup();
				mdns.setup();
				timeSync.setup();
				pages.setup();
				portal.setup();
				ota.setup();
				status.setup();
				logService.setup();
				restart.setup();
				config.setup();
				configService.setup();
				ruleService.setup();

				// the BLE service starts NimBLE, which the scanner, the discovery and the connection manager need
				bleService.setup();
				bleScanner.setup();
				bleDiscovery.setup();
				bleConnectionManager.setup();
				relay.start();
				webServer.begin();
			})
		.every(LED_UPDATE_MS, [](uint32_t dtMs)
			{
				led.update(dtMs);
			})
		.every(HEARTBEAT_MS, [](uint32_t dtMs)
			{
				heartbeat.update(dtMs);
			})
		.every(BATTERY_UPDATE_MS, [](uint32_t dtMs)
			{
				battery.update(dtMs);
			})
		.every(WIFI_UPDATE_MS, [](uint32_t dtMs)
			{
				wifi.update(dtMs);
			})
		.every(PORTAL_UPDATE_MS, [](uint32_t dtMs)
			{
				portal.update(dtMs);
			})
		.every(OTA_UPDATE_MS, [](uint32_t dtMs)
			{
				ota.update(dtMs);
			})
		.every(CONFIG_UPDATE_MS, [](uint32_t dtMs)
			{
				config.update(dtMs);
			})
		.every(RESTART_UPDATE_MS, [](uint32_t dtMs)
			{
				restart.update(dtMs);
			})
		.every(TIME_UPDATE_MS, [](uint32_t dtMs)
			{
				timeSync.update(dtMs);
			})
		.every(BLE_UPDATE_MS, [](uint32_t dtMs)
			{
				bleDiscovery.update(dtMs);
			})
		.start();

	w2b::Logger::info("main", "led gpio %d active-%s, pattern by WiFi state, updated every %u ms; log ring %u B", LED_GPIO,
		LED_ACTIVE_LOW ? "low" : "high", (unsigned)LED_UPDATE_MS, (unsigned)logRing.capacity());
}

// Both cores run their own scheduler thread; Arduino's loopTask is not needed.
void loop()
{
	vTaskDelete(nullptr);
}
