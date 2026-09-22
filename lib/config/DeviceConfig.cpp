#include "DeviceConfig.h"

#include "ConfigRules.h"
#include "JsonMembers.h"

namespace w2b
{

DeviceConfig::DeviceConfig(const Hostname& hostname, LogLevel logLevel, const TimeZone& zone, bool useRandomBleAddress, uint16_t heartbeatS,
	uint32_t batteryScalePpm)
	: name(hostname), level(logLevel), zone(&zone), randomBleAddress(useRandomBleAddress), heartbeatPeriodS(heartbeatS),
		batteryScale(batteryScalePpm)
{
}

bool DeviceConfig::validate(std::string& error) const
{
	if (!name.fits() || !isValidHostname(name.c_str()))
	{
		error = "hostname: \"" + std::string(name.c_str()) + "\" must be 1-63 letters, digits or hyphens, no hyphen at either end";
		return false;
	}

	if (heartbeatPeriodS > kMaxHeartbeatS)
	{
		error = "heartbeat_s: " + std::to_string(heartbeatPeriodS) + " is above " + std::to_string(kMaxHeartbeatS);
		return false;
	}

	if (batteryScale < kUncalibratedBatteryScalePpm - kMaxBatteryCorrectionPpm || batteryScale > kUncalibratedBatteryScalePpm + kMaxBatteryCorrectionPpm)
	{
		error = "battery_scale_ppm: " + std::to_string(batteryScale) + " is more than " + std::to_string(kMaxBatteryCorrectionPpm) + " from " +
			std::to_string(kUncalibratedBatteryScalePpm);
		return false;
	}

	return true;
}

void DeviceConfig::toJson(JsonObject& json) const
{
	json["hostname"] = name.c_str();
	json["log_level"] = logLevelName(level);
	json["timezone"] = zone->name();
	json["use_random_ble_address"] = randomBleAddress;
	json["heartbeat_s"] = heartbeatPeriodS;
	json["battery_scale_ppm"] = batteryScale;
}

bool DeviceConfig::fromJson(const JsonObjectConst& json, std::string& error)
{
	const char* levelName = logLevelName(level);
	const char* zoneName = zone->name();

	if (!readOptionalString(json, "hostname", name, error) || !readOptionalText(json, "log_level", levelName, error) ||
		!readOptionalText(json, "timezone", zoneName, error) || !readOptionalBool(json, "use_random_ble_address", randomBleAddress, error) ||
		!readOptionalUint(json, "heartbeat_s", heartbeatPeriodS, error) || !readOptionalUint(json, "battery_scale_ppm", batteryScale, error))
	{
		return false;
	}

	if (!parseLogLevel(levelName, level))
	{
		error = "log_level: \"" + std::string(levelName) + "\" is not one of error, warn, info, debug";
		return false;
	}

	const TimeZone* found = findTimeZone(zoneName);

	if (found == nullptr)
	{
		error = "timezone: \"" + std::string(zoneName) + "\" is not a known time zone name, e.g. \"Europe/Berlin\" or \"UTC\"";
		return false;
	}

	zone = found;

	return validate(error);
}

bool DeviceConfig::operator==(const DeviceConfig& other) const
{
	return name == other.name && level == other.level && zone == other.zone && randomBleAddress == other.randomBleAddress &&
		heartbeatPeriodS == other.heartbeatPeriodS && batteryScale == other.batteryScale;
}

}  // namespace w2b
