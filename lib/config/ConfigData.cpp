#include "ConfigData.h"

#include <cstdio>
#include <cstring>

#include "JsonMembers.h"

namespace w2b
{

namespace
{

std::string ruleAt(size_t index)
{
	return "rules[" + std::to_string(index) + "]";
}

}  // namespace

ConfigData::ConfigData(const DeviceConfig& device, const WifiConfig& wifi, const Rules& rules)
	: deviceSettings(device), wifiSettings(wifi), ruleList(rules)
{
}

const RuleConfig* ConfigData::findRule(const char* id) const
{
	for (const RuleConfig& rule : ruleList)
	{
		if (rule.id() == id)
		{
			return &rule;
		}
	}

	return nullptr;
}

size_t ConfigData::activeRules() const
{
	size_t count = 0;

	for (const RuleConfig& rule : ruleList)
	{
		count += rule.enabled() ? 1 : 0;
	}

	return count;
}

void ConfigData::reset(const DeviceConfig& device)
{
	deviceSettings = device;
	wifiSettings = WifiConfig();
	ruleList.clear();
}

void ConfigData::setDevice(const DeviceConfig& device)
{
	deviceSettings = device;
}

void ConfigData::setWifi(const WifiConfig& wifi)
{
	wifiSettings = wifi;
}

RuleConfig::Id ConfigData::addRule(const RuleConfig& rule)
{
	const RuleConfig::Id id = freeRuleId();

	ruleList.push_back(rule);
	ruleList.back().assignId(id);

	return id;
}

bool ConfigData::replaceRule(const char* id, const RuleConfig& rule, std::string& error)
{
	for (RuleConfig& stored : ruleList)
	{
		if (stored.id() == id)
		{
			stored = rule;
			stored.assignId(id);
			return true;
		}
	}

	error = "no rule \"" + std::string(id) + "\"";

	return false;
}

bool ConfigData::removeRule(const char* id, std::string& error)
{
	for (size_t i = 0; i < ruleList.size(); ++i)
	{
		if (ruleList[i].id() == id)
		{
			ruleList.erase(ruleList.begin() + i);
			return true;
		}
	}

	error = "no rule \"" + std::string(id) + "\"";

	return false;
}

bool ConfigData::validate(std::string& error) const
{
	if (!deviceSettings.validate(error))
	{
		error = "device." + error;
		return false;
	}

	if (!wifiSettings.validate(error))
	{
		error = "wifi." + error;
		return false;
	}

	return validateRules(error);
}

void ConfigData::toJson(JsonObject& json) const
{
	json["schema"] = kSchema;
	writeObject(json, "device", deviceSettings);
	writeObject(json, "wifi", wifiSettings);

	JsonArray rules = json["rules"].to<JsonArray>();

	for (const RuleConfig& rule : ruleList)
	{
		JsonObject item = rules.add<JsonObject>();
		rule.toJson(item);
	}
}

bool ConfigData::fromJson(const JsonObjectConst& json, std::string& error)
{
	uint32_t schema = 0;

	if (!readUint(json, "schema", schema, error))
	{
		return false;
	}

	if (schema != kSchema)
	{
		error = "schema: version " + std::to_string(schema) + " not supported, expected " + std::to_string(kSchema);
		return false;
	}

	return readOptionalObject(json, "device", deviceSettings, error) && readOptionalObject(json, "wifi", wifiSettings, error) &&
		readRules(json, error) && validate(error);
}

bool ConfigData::operator==(const ConfigData& other) const
{
	return deviceSettings == other.deviceSettings && wifiSettings == other.wifiSettings && ruleList == other.ruleList;
}

RuleConfig::Id ConfigData::freeRuleId() const
{
	char id[16];

	for (unsigned number = 0;; ++number)
	{
		std::snprintf(id, sizeof(id), "id%u", number);

		if (findRule(id) == nullptr)
		{
			return RuleConfig::Id(id);
		}
	}
}

// Each rule starts from the defaults of the keys it leaves out; ids go to the rules without one once all are read.
bool ConfigData::readRules(const JsonObjectConst& json, std::string& error)
{
	JsonVariantConst member = json["rules"];

	if (member.isNull())
	{
		return true;
	}

	if (!member.is<JsonArrayConst>())
	{
		error = "rules: expected an array";
		return false;
	}

	JsonArrayConst items = member.as<JsonArrayConst>();
	ruleList.clear();
	ruleList.reserve(items.size());

	for (JsonVariantConst item : items)
	{
		const std::string at = ruleAt(ruleList.size());
		RuleConfig rule = SerializableFactory::createEmpty<RuleConfig>();

		if (!item.is<JsonObjectConst>())
		{
			error = at + ": expected an object";
			return false;
		}

		if (!rule.fromJson(item.as<JsonObjectConst>(), error))
		{
			error = at + "." + error;
			return false;
		}

		ruleList.push_back(rule);
	}

	for (RuleConfig& rule : ruleList)
	{
		if (rule.id().empty())
		{
			rule.assignId(freeRuleId());
		}
	}

	return true;
}

bool ConfigData::validateRules(std::string& error) const
{
	if (activeRules() > kMaxActiveRules)
	{
		error = "rules: " + std::to_string(activeRules()) + " enabled, at most " + std::to_string(kMaxActiveRules) +
			" at a time";
		return false;
	}

	for (size_t i = 0; i < ruleList.size(); ++i)
	{
		if (!ruleList[i].validate(error))
		{
			error = ruleAt(i) + "." + error;
			return false;
		}

		if (ruleList[i].id().empty())
		{
			error = ruleAt(i) + ".id: missing";
			return false;
		}

		if (!validateUnique(i, error))
		{
			return false;
		}
	}

	return true;
}

// Against the rules before it, so the later of two rules reports the clash.
bool ConfigData::validateUnique(size_t index, std::string& error) const
{
	const RuleConfig& rule = ruleList[index];

	for (size_t i = 0; i < index; ++i)
	{
		const RuleConfig& other = ruleList[i];
		const std::string otherId = other.id().c_str();

		if (rule.id() == other.id())
		{
			error = ruleAt(index) + ".id: \"" + otherId + "\" is the id of " + ruleAt(i);
			return false;
		}

		if (rule.enabled() && other.enabled() && rule.listenPort() == other.listenPort())
		{
			error = ruleAt(index) + ".listen_port: " + std::to_string(rule.listenPort()) + " is the port of enabled rule \"" + otherId + "\"";
			return false;
		}
	}

	return true;
}

}  // namespace w2b
