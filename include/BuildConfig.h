// Compile-time settings with defaults; platformio.ini overrides any with -D NAME=value. The DEFAULT_* values seed /config.json when
// it is missing or broken.
#pragma once

// the firmware version, set by platformio.ini
#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif

// the serial console's baud rate
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif

// the serial console's transmit buffer: a log line returns once its bytes are in it and the UART interrupt sends them; a line beyond
// the room left waits for it
#ifndef SERIAL_TX_BUFFER_BYTES
#define SERIAL_TX_BUFFER_BYTES 1024
#endif

// the status LED's GPIO; LOLIN32 Lite layout, a hardware fact of the board's env
#ifndef LED_GPIO
#define LED_GPIO 22
#endif

// 1: the LED sits between 3.3 V and the pin, so on is LOW
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 1
#endif

// the data partition mounted as LittleFS; min_spiffs.csv names it "spiffs", 128 KB at 0x3D0000
#ifndef FS_PARTITION_LABEL
#define FS_PARTITION_LABEL "spiffs"
#endif

// the hostname of a new configuration
#ifndef DEFAULT_HOSTNAME
#define DEFAULT_HOSTNAME "w2b"
#endif

// the log level of a new configuration: error, warn, info or debug
#ifndef DEFAULT_LOG_LEVEL
#define DEFAULT_LOG_LEVEL "debug"
#endif

// the time zone of a new configuration, an IANA name of lib/time/TimeZones
#ifndef DEFAULT_TIMEZONE
#define DEFAULT_TIMEZONE "UTC"
#endif

// a new configuration's BLE address: false the chip's public one, true a random static one, new at each start
#ifndef DEFAULT_USE_RANDOM_BLE_ADDRESS
#define DEFAULT_USE_RANDOM_BLE_ADDRESS false
#endif

// the logger's RAM ring, bytes
#ifndef LOG_RING_BYTES
#define LOG_RING_BYTES 8192
#endif

// NTP servers; both serve UTC without leap smearing, so they agree: the NTP Pool's round robin near the relay, Cloudflare's anycast
#ifndef NTP_SERVER_1
#define NTP_SERVER_1 "pool.ntp.org"
#endif

// the second NTP server
#ifndef NTP_SERVER_2
#define NTP_SERVER_2 "time.cloudflare.com"
#endif

// the relay scheduler's core (BLE and TCP transport), away from the radio stacks on core 0
#ifndef RELAY_CORE
#define RELAY_CORE 1
#endif

// the relay thread's FreeRTOS priority
#ifndef RELAY_PRIORITY
#define RELAY_PRIORITY 3
#endif

// the relay thread's stack
#ifndef RELAY_STACK_BYTES
#define RELAY_STACK_BYTES 5120
#endif

// the application scheduler's core (web, WiFi, config and the rest), beside the radio stacks, which run first
#ifndef APPLICATION_CORE
#define APPLICATION_CORE 0
#endif

// the application thread's FreeRTOS priority
#ifndef APPLICATION_PRIORITY
#define APPLICATION_PRIORITY 1
#endif

// the application thread's stack
#ifndef APPLICATION_STACK_BYTES
#define APPLICATION_STACK_BYTES 6144
#endif

// LED pattern steps
#ifndef LED_UPDATE_MS
#define LED_UPDATE_MS 10
#endif

// how often the heartbeat counts time; its line's period is the configuration's heartbeat_s
#ifndef HEARTBEAT_MS
#define HEARTBEAT_MS 1000
#endif

// the battery: one reading of its pin per update, the voltage the mean of the last BatteryMonitor::kSamples
#ifndef BATTERY_UPDATE_MS
#define BATTERY_UPDATE_MS 50
#endif

// the setup portal's scan
#ifndef PORTAL_UPDATE_MS
#define PORTAL_UPDATE_MS 100
#endif

// the WiFi supervisor
#ifndef WIFI_UPDATE_MS
#define WIFI_UPDATE_MS 100
#endif

// firmware update confirmation and restart
#ifndef OTA_UPDATE_MS
#define OTA_UPDATE_MS 100
#endif

// Config: saves a submitted change
#ifndef CONFIG_UPDATE_MS
#define CONFIG_UPDATE_MS 100
#endif

// a requested restart
#ifndef RESTART_UPDATE_MS
#define RESTART_UPDATE_MS 100
#endif

// the BLE probe on the application thread: how often its state machine runs
#ifndef BLE_UPDATE_MS
#define BLE_UPDATE_MS 100
#endif

// the relay thread's tasks, the BLE scan and connections with their TCP servers: the latency the relay adds each way is at most this
#ifndef RELAY_UPDATE_MS
#define RELAY_UPDATE_MS 10
#endif

// NTP sync log lines
#ifndef TIME_UPDATE_MS
#define TIME_UPDATE_MS 1000
#endif

// the setup access point's name; it is open, no password
#ifndef PORTAL_AP_SSID
#define PORTAL_AP_SSID "w2b-setup"
#endif
