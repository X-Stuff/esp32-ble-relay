#include "RuleConfig.h"

#include "ConfigRules.h"
#include "JsonMembers.h"

namespace w2b
{

namespace
{

constexpr const char* kOversizes[] = {"chunk", "long_write", "drop"};
constexpr const char* kConnectPolicies[] = {"on_demand", "always"};

bool outOfRange(const char* field, uint32_t value, uint32_t min, uint32_t max, std::string& error)
{
	if (value >= min && value <= max)
	{
		return false;
	}

	error = std::string(field) + ": " + std::to_string(value) + " is out of range " + std::to_string(min) + "-" + std::to_string(max);

	return true;
}

}  // namespace

RuleConfig::RuleConfig(const Id& id, const Name& name, bool enabled, uint16_t listenPort, uint8_t maxClients, const TargetConfig& target,
	const BleUuid& serviceUuid, const TxConfig& tx, const RxConfig& rx, uint16_t coalesceMs, Oversize oversize,
	ConnectPolicy connectPolicy, uint16_t idleS, const Backoff& reconnectBackoffS, bool closeTcpOnBleLoss, bool hexdump)
	: ident(id), label(name), active(enabled), port(listenPort), clientLimit(maxClients), peer(target), service(serviceUuid), txChannel(tx),
		rxChannel(rx), coalesceWindowMs(coalesceMs), oversizePolicy(oversize), policy(connectPolicy), idleTimeS(idleS),
		backoff(reconnectBackoffS), closeOnLoss(closeTcpOnBleLoss), dumpHex(hexdump)
{
}

size_t RuleConfig::reconnectSteps() const
{
	size_t steps = 0;

	while (steps < backoff.size() && backoff[steps] != 0)
	{
		++steps;
	}

	return steps;
}

bool RuleConfig::validate(std::string& error) const
{
	return validateIdentity(error) && validateLink(error) && validateTiming(error);
}

void RuleConfig::toJson(JsonObject& json) const
{
	json["id"] = ident.c_str();
	json["name"] = label.c_str();
	json["enabled"] = active;
	json["listen_port"] = port;
	json["max_clients"] = clientLimit;
	writeObject(json, "target", peer);
	json["service_uuid"] = service.text().c_str();
	writeObject(json, "tx", txChannel);
	writeObject(json, "rx", rxChannel);
	json["coalesce_ms"] = coalesceWindowMs;
	json["oversize"] = choiceName(oversizePolicy, kOversizes);
	json["connect_policy"] = choiceName(policy, kConnectPolicies);
	json["idle_s"] = idleTimeS;

	JsonArray steps = json["reconnect_backoff_s"].to<JsonArray>();

	for (size_t i = 0; i < reconnectSteps(); ++i)
	{
		steps.add(backoff[i]);
	}

	json["close_tcp_on_ble_loss"] = closeOnLoss;
	json["hexdump"] = dumpHex;
}

bool RuleConfig::fromJson(const JsonObjectConst& json, std::string& error)
{
	return readIdentity(json, error) && readLink(json, error) && readBehaviour(json, error) && validate(error);
}

bool RuleConfig::operator==(const RuleConfig& other) const
{
	return ident == other.ident && label == other.label && active == other.active && port == other.port && clientLimit == other.clientLimit &&
		peer == other.peer && service == other.service && txChannel == other.txChannel && rxChannel == other.rxChannel &&
		coalesceWindowMs == other.coalesceWindowMs && oversizePolicy == other.oversizePolicy && policy == other.policy &&
		idleTimeS == other.idleTimeS && backoff == other.backoff && closeOnLoss == other.closeOnLoss &&
		dumpHex == other.dumpHex;
}

void RuleConfig::assignId(const Id& id)
{
	ident = id;
}

bool RuleConfig::readIdentity(const JsonObjectConst& json, std::string& error)
{
	return readOptionalString(json, "id", ident, error) && readOptionalString(json, "name", label, error) &&
		readOptionalBool(json, "enabled", active, error) && readOptionalUint(json, "listen_port", port, error) &&
		readOptionalUint(json, "max_clients", clientLimit, error);
}

bool RuleConfig::readLink(const JsonObjectConst& json, std::string& error)
{
	const char* uuid = nullptr;

	if (!readObject(json, "target", peer, error) || !readText(json, "service_uuid", uuid, error))
	{
		return false;
	}

	if (!BleUuid::parse(uuid, service) || service.empty())
	{
		error = "service_uuid: \"" + std::string(uuid) + "\" is not a GATT UUID, e.g. \"fee7\" or \"0000fee7-0000-1000-8000-00805f9b34fb\"";
		return false;
	}

	return readOptionalObject(json, "tx", txChannel, error) && readOptionalObject(json, "rx", rxChannel, error);
}

bool RuleConfig::readBehaviour(const JsonObjectConst& json, std::string& error)
{
	return readOptionalUint(json, "coalesce_ms", coalesceWindowMs, error) && readOptionalChoice(json, "oversize", oversizePolicy, kOversizes, error) &&
		readOptionalChoice(json, "connect_policy", policy, kConnectPolicies, error) && readOptionalUint(json, "idle_s", idleTimeS, error) &&
		readBackoff(json, error) && readOptionalBool(json, "close_tcp_on_ble_loss", closeOnLoss, error) &&
		readOptionalBool(json, "hexdump", dumpHex, error);
}

// Missing or null keeps the list; an array replaces it as a whole. A 0 would end the list early, so it is refused here.
bool RuleConfig::readBackoff(const JsonObjectConst& json, std::string& error)
{
	JsonVariantConst member = json["reconnect_backoff_s"];

	if (member.isNull())
	{
		return true;
	}

	if (!member.is<JsonArrayConst>())
	{
		error = "reconnect_backoff_s: expected an array of whole numbers";
		return false;
	}

	JsonArrayConst items = member.as<JsonArrayConst>();
	Backoff steps{};
	size_t count = 0;

	if (items.size() > steps.size())
	{
		error = "reconnect_backoff_s: at most " + std::to_string(kMaxBackoffSteps) + " steps";
		return false;
	}

	for (JsonVariantConst step : items)
	{
		if (!step.is<uint16_t>())
		{
			error = "reconnect_backoff_s: expected whole numbers from 0 to 65535";
			return false;
		}

		if (outOfRange("reconnect_backoff_s", step.as<uint16_t>(), 1, kMaxBackoffS, error))
		{
			return false;
		}

		steps[count++] = step.as<uint16_t>();
	}

	backoff = steps;

	return true;
}

bool RuleConfig::validateIdentity(std::string& error) const
{
	if (!ident.fits() || (!ident.empty() && !isValidRuleId(ident.c_str())))
	{
		error = "id: \"" + std::string(ident.c_str()) + "\" must be 1-32 letters, digits, hyphens or underscores";
		return false;
	}

	if (!label.fits() || !isValidLabel(label.c_str()))
	{
		error = "name: at most 63 bytes, no control characters";
		return false;
	}

	if (outOfRange("listen_port", port, 1, 65535, error))
	{
		return false;
	}

	if (port == kWebPort)
	{
		error = "listen_port: " + std::to_string(port) + " is the web page's port";
		return false;
	}

	return !outOfRange("max_clients", clientLimit, 1, kMaxClients, error);
}

bool RuleConfig::validateLink(std::string& error) const
{
	if (!peer.validate(error))
	{
		error = "target." + error;
		return false;
	}

	if (service.empty())
	{
		error = "service_uuid: missing";
		return false;
	}

	if (!txChannel.validate(error))
	{
		error = "tx." + error;
		return false;
	}

	if (!rxChannel.validate(error))
	{
		error = "rx." + error;
		return false;
	}

	if (txChannel.mode() == ChannelMode::None && rxChannel.mode() == ChannelMode::None)
	{
		error = "rx.mode and tx.mode are None; a valid rule needs TX, RX or both";
		return false;
	}

	return true;
}

bool RuleConfig::validateTiming(std::string& error) const
{
	if (outOfRange("coalesce_ms", coalesceWindowMs, 0, kMaxCoalesceMs, error) || outOfRange("idle_s", idleTimeS, 0, kMaxIdleS, error))
	{
		return false;
	}

	const size_t steps = reconnectSteps();

	if (steps == 0)
	{
		error = "reconnect_backoff_s: 1-" + std::to_string(kMaxBackoffSteps) + " steps";
		return false;
	}

	// a wait after the first 0 would never be used
	for (size_t i = steps; i < backoff.size(); ++i)
	{
		if (backoff[i] != 0)
		{
			error = "reconnect_backoff_s: 0 is out of range 1-" + std::to_string(kMaxBackoffS);
			return false;
		}
	}

	for (size_t i = 0; i < steps; ++i)
	{
		if (outOfRange("reconnect_backoff_s", backoff[i], 1, kMaxBackoffS, error))
		{
			return false;
		}
	}

	return true;
}

}  // namespace w2b
