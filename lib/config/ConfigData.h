#pragma once

#ifndef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#error "CONFIG_BT_NIMBLE_MAX_CONNECTIONS comes from platformio.ini's [common] build_flags"
#endif

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "DeviceConfig.h"
#include "JsonSerializable.h"
#include "RuleConfig.h"
#include "WifiConfig.h"

namespace w2b
{

// The content of /config.json: schema, "device", "wifi" and "rules", the rules on the heap. Lives in Config and is edited in place.
// The default state is valid: the defaults of every section, no rules.
class ConfigData : public IJsonSerializable<ConfigData>
{
public:
	// the format version, "schema"
	static constexpr uint32_t kSchema = 1;

	// enabled rules at a time: one BLE connection each at most; the last of NimBLE's (platformio.ini) is for discovery
	static constexpr size_t kMaxActiveRules = CONFIG_BT_NIMBLE_MAX_CONNECTIONS - 1;

	// the rules in the order of the file, as many as the file's size limit takes
	using Rules = std::vector<RuleConfig>;

	// the defaults of every section, no rules
	ConfigData() = default;

	// every section
	ConfigData(const DeviceConfig& device, const WifiConfig& wifi, const Rules& rules);

	// hostname, log level, time zone
	inline const DeviceConfig& device() const { return deviceSettings; }

	// the station's settings
	inline const WifiConfig& wifi() const { return wifiSettings; }

	// every rule, enabled or not
	inline const Rules& rules() const { return ruleList; }

	// The rule with the id; null when none has it.
	const RuleConfig* findRule(const char* id) const;

	// rules enabled
	size_t activeRules() const;

	// Replaces everything with device and the defaults of the other sections: what a missing or broken file becomes.
	void reset(const DeviceConfig& device);

	// replaces the device section
	void setDevice(const DeviceConfig& device);

	// replaces the wifi section
	void setWifi(const WifiConfig& wifi);

	// Adds rule at the end under the first free id of "id0", "id1", ...; returns that id. rule's own id is not used.
	RuleConfig::Id addRule(const RuleConfig& rule);

	// Replaces the rule with the id, in its place, keeping the id; false with the reason when none has it.
	bool replaceRule(const char* id, const RuleConfig& rule, std::string& error);

	// Removes the rule with the id; false with the reason when none has it.
	bool removeRule(const char* id, std::string& error);

	// Every section and rule; at most kMaxActiveRules enabled; ids unique, listen ports unique among the enabled rules.
	bool validate(std::string& error) const;

	// schema and every section
	void toJson(JsonObject& json) const override;

	// "schema" required and equal to kSchema; a missing section keeps its value; "rules" replaces every rule, one without an id gets one.
	bool fromJson(const JsonObjectConst& json, std::string& error) override;

	// every section equal
	bool operator==(const ConfigData& other) const;

private:
	RuleConfig::Id freeRuleId() const;

	bool readRules(const JsonObjectConst& json, std::string& error);

	bool validateRules(std::string& error) const;

	bool validateUnique(size_t index, std::string& error) const;

	DeviceConfig deviceSettings;

	WifiConfig wifiSettings;

	Rules ruleList;
};

}  // namespace w2b
