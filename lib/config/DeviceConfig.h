#pragma once

#include <cstdint>
#include <string>

#include "FixedString.h"
#include "JsonSerializable.h"
#include "Logger.h"
#include "TimeZones.h"

namespace w2b
{

// Device settings, "device" in /config.json: hostname, log level, time zone, BLE address, heartbeat period, battery scale. The default
// state is valid: w2b, info, UTC, the public BLE address, a heartbeat every kDefaultHeartbeatS, an uncalibrated battery.
class DeviceConfig : public IJsonSerializable<DeviceConfig>
{
public:
	// the longest period between two heartbeat lines, seconds
	static constexpr uint16_t kMaxHeartbeatS = 3600;

	// the period of a new configuration, seconds
	static constexpr uint16_t kDefaultHeartbeatS = 15;

	// a battery scale of 1 in parts per million, the divider's ratio alone: a new configuration's
	static constexpr uint32_t kUncalibratedBatteryScalePpm = 1000000;

	// how far the battery scale may be from kUncalibratedBatteryScalePpm, parts per million: 10 %
	static constexpr uint32_t kMaxBatteryCorrectionPpm = 100000;

	// an RFC 1123 label
	using Hostname = FixedString<63>;

	// w2b, info, UTC, the public BLE address, a heartbeat every kDefaultHeartbeatS, an uncalibrated battery
	DeviceConfig() = default;

	// zone: an entry of the TimeZones table; useRandomBleAddress: see useRandomBleAddress(); heartbeatS: see heartbeatS();
	// batteryScalePpm: see batteryScalePpm()
	DeviceConfig(const Hostname& hostname, LogLevel logLevel, const TimeZone& zone, bool useRandomBleAddress, uint16_t heartbeatS,
		uint32_t batteryScalePpm);

	// the mDNS and DHCP name
	inline const Hostname& hostname() const { return name; }

	// the lowest level the log keeps
	inline LogLevel logLevel() const { return level; }

	// the local time of the log and the status
	inline const TimeZone& timeZone() const { return *zone; }

	// BLE connects and scans use a random static address, new at each start, instead of the chip's public one; read at start
	inline bool useRandomBleAddress() const { return randomBleAddress; }

	// seconds between two heartbeat lines in the log; 0: no line
	inline uint16_t heartbeatS() const { return heartbeatPeriodS; }

	// the factor on the battery's voltage from the pin and the divider, parts per million; kUncalibratedBatteryScalePpm: none
	inline uint32_t batteryScalePpm() const { return batteryScale; }

	// The hostname fits and is an RFC 1123 label; the heartbeat period is at most kMaxHeartbeatS; the battery scale is at most
	// kMaxBatteryCorrectionPpm from kUncalibratedBatteryScalePpm.
	bool validate(std::string& error) const;

	// hostname, log_level, timezone, use_random_ble_address, heartbeat_s, battery_scale_ppm
	void toJson(JsonObject& json) const override;

	// Keys the JSON leaves out keep their value. log_level: error, warn, info, debug; timezone: a name of the TimeZones table;
	// heartbeat_s: 0 to kMaxHeartbeatS.
	bool fromJson(const JsonObjectConst& json, std::string& error) override;

	// every field equal
	bool operator==(const DeviceConfig& other) const;

private:
	Hostname name = "w2b";

	LogLevel level = LogLevel::Info;

	// never null
	const TimeZone* zone = &defaultTimeZone();

	bool randomBleAddress = false;

	uint16_t heartbeatPeriodS = kDefaultHeartbeatS;

	uint32_t batteryScale = kUncalibratedBatteryScalePpm;
};

}  // namespace w2b
