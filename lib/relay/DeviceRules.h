#pragma once

#include <cstdint>
#include <vector>

#include "ConfigData.h"
#include "RuleConfig.h"
#include "TargetConfig.h"

namespace w2b
{

// The enabled rules for one device, which share one BLE connection; the device by address, or by name when its rules have none
class DeviceRules
{
public:
	// Groups the enabled rules of rules by device, the devices in the order of their first rule.
	static std::vector<DeviceRules> groupByDevice(const ConfigData::Rules& rules);

	// first: the device's first rule; its target is the device
	explicit DeviceRules(const RuleConfig& first);

	// the device: an address, or a name (the strongest device with it is used)
	inline const TargetConfig& target() const { return deviceTarget; }

	// in the order of the configuration
	inline const std::vector<RuleConfig>& rules() const { return ruleList; }

	// true when rule's target is this device: the same address, or no address on both and the same name
	bool matchesTarget(const RuleConfig& rule) const;

	// True when a rule needs the connection up now: policy always. Policy on demand waits for a TCP client.
	bool wantsLink() const;

	// The wait before reconnect attempt, 0 for the first retry, in ms: from the first rule's list, its last value repeating.
	uint32_t backoffMs(uint32_t attempt) const;

	// how many waits the first rule's list holds; from attempt backoffSteps() - 1 on, backoffMs() gives the last
	size_t backoffSteps() const;

	// same device and the same rules
	bool operator==(const DeviceRules& other) const;

	// another device or other rules
	inline bool operator!=(const DeviceRules& other) const { return !(*this == other); }

private:
	TargetConfig deviceTarget;

	std::vector<RuleConfig> ruleList;
};

}  // namespace w2b
