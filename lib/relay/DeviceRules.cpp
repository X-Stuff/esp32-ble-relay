#include "DeviceRules.h"

#include <algorithm>

namespace w2b
{

std::vector<DeviceRules> DeviceRules::groupByDevice(const ConfigData::Rules& rules)
{
	std::vector<DeviceRules> devices;

	for (const RuleConfig& rule : rules)
	{
		if (!rule.enabled())
		{
			continue;
		}

		auto known = std::find_if(devices.begin(), devices.end(),
			[&rule](const DeviceRules& device)
			{
				return device.matchesTarget(rule);
			});

		if (known != devices.end())
		{
			known->ruleList.push_back(rule);
		}
		else
		{
			devices.push_back(DeviceRules(rule));
		}
	}

	return devices;
}

DeviceRules::DeviceRules(const RuleConfig& first)
	: deviceTarget(first.target()), ruleList{first}
{
}

bool DeviceRules::matchesTarget(const RuleConfig& rule) const
{
	const TargetConfig& other = rule.target();

	if (deviceTarget.address() || other.address())
	{
		return deviceTarget.address() == other.address();
	}

	return deviceTarget.name() == other.name();
}

bool DeviceRules::wantsLink() const
{
	return std::any_of(ruleList.begin(), ruleList.end(),
		[](const RuleConfig& rule)
		{
			return rule.connectPolicy() == RuleConfig::ConnectPolicy::Always;
		});
}

uint32_t DeviceRules::backoffMs(uint32_t attempt) const
{
	const RuleConfig& first = ruleList.front();
	const size_t steps = first.reconnectSteps();

	if (steps == 0)
	{
		return 0;
	}

	return static_cast<uint32_t>(first.reconnectBackoffS()[std::min<size_t>(attempt, steps - 1)]) * 1000;
}

size_t DeviceRules::backoffSteps() const
{
	return ruleList.front().reconnectSteps();
}

bool DeviceRules::operator==(const DeviceRules& other) const
{
	return deviceTarget == other.deviceTarget && ruleList == other.ruleList;
}

}  // namespace w2b
