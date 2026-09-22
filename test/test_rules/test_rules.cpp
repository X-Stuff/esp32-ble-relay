#include <unity.h>

#include <cstring>
#include <optional>
#include <string>

#include "ConfigData.h"
#include "ConfigRules.h"
#include "JsonSerializable.h"
#include "JsonText.h"

using w2b::BleAddress;
using w2b::BleUuid;
using w2b::ChannelMode;
using w2b::ConfigData;
using w2b::DeviceConfig;
using w2b::JsonText;
using w2b::LogLevel;
using w2b::RuleConfig;
using w2b::RxConfig;
using w2b::SerializableFactory;
using w2b::TargetConfig;
using w2b::TxConfig;
using w2b::WifiConfig;

namespace
{

// The HF-LPT270 rule, in the order toJson writes it.
const char* const kPlanRule =
	R"({"id":"inv1","name":"Renac inverter","enabled":true,"listen_port":8899,"max_clients":1,)"
	R"("target":{"address":"12:34:56:78:9a:bc","name":"HF-LPT270"},"service_uuid":"fee7",)"
	R"("tx":{"mode":"uuid","uuid":"fed5","write_with_response":true},"rx":{"mode":"uuid","uuid":"fed6","subscribe":"indicate"},)"
	R"("coalesce_ms":10,"oversize":"chunk","connect_policy":"on_demand","idle_s":3,)"
	R"("reconnect_backoff_s":[2,4,8,10,30],"close_tcp_on_ble_loss":false,"hexdump":false})";

// a channel, which every valid rule needs
const std::string kTx = R"("tx":{"mode":"uuid","uuid":"fed5"})";

const std::string kRx = R"("rx":{"mode":"uuid","uuid":"fed6"})";

// The required keys, a TX, the port, and whether it is enabled.
std::string minimalRule(const std::string& id, const std::string& address, int port, bool enabled = true)
{
	return R"({"id":")" + id + R"(","target":{"address":")" + address + R"("},"service_uuid":"fee7",)" + kTx + R"(,"listen_port":)" +
		std::to_string(port) + R"(,"enabled":)" + (enabled ? "true" : "false") + "}";
}

// A rule that finds its device by name.
std::string namedRule(const std::string& id, const std::string& name, int port)
{
	return R"({"id":")" + id + R"(","target":{"name":")" + name + R"("},"service_uuid":"fee7",)" + kTx + R"(,"listen_port":)" +
		std::to_string(port) + "}";
}

// A valid rule body with the keys of extra added; kTx and kRx where extra names no channel of its own.
std::string ruleWith(const std::string& extra)
{
	const std::string tx = extra.find("\"tx\"") == std::string::npos ? "," + kTx : "";
	const std::string rx = extra.find("\"rx\"") == std::string::npos ? "," + kRx : "";

	return "{" + extra + tx + rx + R"(,"id":"inv1","target":{"address":"12:34:56:78:9a:bc"},"service_uuid":"fee7"})";
}

bool parseRule(const std::string& text, RuleConfig& rule, std::string& error)
{
	rule = SerializableFactory::createEmpty<RuleConfig>();

	return JsonText::read(text, rule, error);
}

bool ruleOk(const std::string& text, std::string& error)
{
	RuleConfig rule = SerializableFactory::createEmpty<RuleConfig>();

	return parseRule(text, rule, error);
}

BleUuid uuid(const char* text)
{
	BleUuid parsed;
	TEST_ASSERT_TRUE_MESSAGE(BleUuid::parse(text, parsed), text);

	return parsed;
}

BleAddress address(const char* text)
{
	BleAddress parsed;
	TEST_ASSERT_TRUE_MESSAGE(BleAddress::parse(text, parsed), text);

	return parsed;
}

ConfigData emptyConfig()
{
	return ConfigData(DeviceConfig("w2b", LogLevel::Info, w2b::defaultTimeZone(), false, DeviceConfig::kDefaultHeartbeatS,
		DeviceConfig::kUncalibratedBatteryScalePpm), WifiConfig("", "", std::nullopt), {});
}

bool parseConfig(const std::string& rules, ConfigData& out, std::string& error)
{
	return JsonText::read(R"({"schema":1,"rules":[)" + rules + "]}", out, error);
}

std::string joined(int count, int firstPort, bool enabled)
{
	std::string rules;

	for (int i = 0; i < count; ++i)
	{
		const std::string suffix = std::to_string(i + 10);
		rules += (i == 0 ? "" : ",") + minimalRule("r" + suffix, "00:00:00:00:00:" + suffix, firstPort + i, enabled);
	}

	return rules;
}

}  // namespace

void setUp()
{
}

void tearDown()
{
}

static void test_the_hf_lpt270_rule_round_trips_key_for_key()
{
	RuleConfig rule = SerializableFactory::createEmpty<RuleConfig>();
	std::string error;

	TEST_ASSERT_TRUE_MESSAGE(parseRule(kPlanRule, rule, error), error.c_str());
	TEST_ASSERT_EQUAL_STRING(kPlanRule, JsonText::write(rule).c_str());

	TEST_ASSERT_EQUAL_STRING("inv1", rule.id().c_str());
	TEST_ASSERT_TRUE(rule.target().address() == address("12:34:56:78:9a:bc"));
	TEST_ASSERT_EQUAL(static_cast<int>(ChannelMode::Uuid), static_cast<int>(rule.tx().mode()));
	TEST_ASSERT_TRUE(rule.tx().uuid() == uuid("fed5"));
	TEST_ASSERT_EQUAL(static_cast<int>(RxConfig::Subscribe::Indicate), static_cast<int>(rule.rx().subscribe()));
}

static void test_required_keys_only_take_the_defaults()
{
	RuleConfig rule = SerializableFactory::createEmpty<RuleConfig>();
	std::string error;

	TEST_ASSERT_TRUE_MESSAGE(parseRule(R"({"id":"a","target":{"address":"12:34:56:78:9a:bc"},"service_uuid":"fee7",)" + kRx + "}", rule, error),
		error.c_str());

	TEST_ASSERT_TRUE(rule.name().empty());
	TEST_ASSERT_TRUE(rule.enabled());
	TEST_ASSERT_EQUAL_UINT16(RuleConfig::kDefaultPort, rule.listenPort());
	TEST_ASSERT_EQUAL_UINT8(1, rule.maxClients());
	TEST_ASSERT_EQUAL(static_cast<int>(ChannelMode::None), static_cast<int>(rule.tx().mode()));
	TEST_ASSERT_TRUE(rule.tx().uuid().empty());
	TEST_ASSERT_TRUE(rule.tx().writeWithResponse());
	TEST_ASSERT_EQUAL(static_cast<int>(ChannelMode::Uuid), static_cast<int>(rule.rx().mode()));
	TEST_ASSERT_EQUAL(static_cast<int>(RxConfig::Subscribe::Auto), static_cast<int>(rule.rx().subscribe()));
	TEST_ASSERT_EQUAL_UINT16(10, rule.coalesceMs());
	TEST_ASSERT_EQUAL(static_cast<int>(RuleConfig::Oversize::Chunk), static_cast<int>(rule.oversize()));
	TEST_ASSERT_EQUAL(static_cast<int>(RuleConfig::ConnectPolicy::OnDemand), static_cast<int>(rule.connectPolicy()));
	TEST_ASSERT_EQUAL_UINT16(3, rule.idleS());
	TEST_ASSERT_TRUE(rule.reconnectBackoffS() == RuleConfig::Backoff({2, 4, 8, 10, 30}));
	TEST_ASSERT_EQUAL(5, rule.reconnectSteps());
	TEST_ASSERT_FALSE(rule.closeTcpOnBleLoss());
	TEST_ASSERT_FALSE(rule.hexdump());
}

static void test_missing_required_keys_are_named()
{
	std::string error;

	TEST_ASSERT_FALSE(ruleOk(R"({"id":"a","service_uuid":"fee7"})", error));
	TEST_ASSERT_EQUAL_STRING("target: missing", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(R"({"id":"a","target":{},"service_uuid":"fee7"})", error));
	TEST_ASSERT_EQUAL_STRING("target.address: missing, and no name to find the device by", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(R"({"id":"a","target":{"address":"","name":""},"service_uuid":"fee7"})", error));
	TEST_ASSERT_EQUAL_STRING("target.address: missing, and no name to find the device by", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(R"({"id":"a","target":{"address":"12:34:56:78:9a:bc"}})", error));
	TEST_ASSERT_EQUAL_STRING("service_uuid: missing", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(R"({"id":"a","target":{"address":"12:34:56:78:9a:bc"},"service_uuid":""})", error));
	TEST_ASSERT_EQUAL_STRING("service_uuid: \"\" is not a GATT UUID, e.g. \"fee7\" or \"0000fee7-0000-1000-8000-00805f9b34fb\"", error.c_str());
}

static void test_the_id_is_optional_and_the_target_an_address_or_a_name()
{
	RuleConfig rule = SerializableFactory::createEmpty<RuleConfig>();
	std::string error;

	TEST_ASSERT_TRUE_MESSAGE(parseRule(R"({"target":{"address":"12:34:56:78:9a:bc"},"service_uuid":"fee7",)" + kTx + "}", rule, error),
		error.c_str());
	TEST_ASSERT_TRUE(rule.id().empty());

	TEST_ASSERT_TRUE_MESSAGE(parseRule(namedRule("a", "HF-LPT270", 8899), rule, error), error.c_str());
	TEST_ASSERT_FALSE(rule.target().address().has_value());
	TEST_ASSERT_EQUAL_STRING("HF-LPT270", rule.target().name().c_str());

	RuleConfig back = SerializableFactory::createEmpty<RuleConfig>();
	const std::string json = JsonText::write(rule);
	TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), R"("target":{"address":"","name":"HF-LPT270"})"));
	TEST_ASSERT_TRUE_MESSAGE(JsonText::read(json, back, error), error.c_str());
	TEST_ASSERT_TRUE(back == rule);

	// address_type and mtu_request of an older file are ignored
	TEST_ASSERT_TRUE_MESSAGE(ruleOk(R"({"target":{"address":"12:34:56:78:9a:bc","address_type":"random"},"service_uuid":"fee7","mtu_request":22,)" +
		kTx + "}", error), error.c_str());

	// a missing address keeps it, "" clears it
	TargetConfig target = SerializableFactory::createEmpty<TargetConfig>();
	TEST_ASSERT_TRUE_MESSAGE(JsonText::read(R"({"address":"12:34:56:78:9a:bc"})", target, error), error.c_str());
	TEST_ASSERT_TRUE_MESSAGE(JsonText::read(R"({"name":"x"})", target, error), error.c_str());
	TEST_ASSERT_TRUE(target.address() == address("12:34:56:78:9a:bc"));
	TEST_ASSERT_TRUE_MESSAGE(JsonText::read(R"({"address":""})", target, error), error.c_str());
	TEST_ASSERT_FALSE(target.address().has_value());
	TEST_ASSERT_FALSE(JsonText::read(R"({"name":""})", target, error));
	TEST_ASSERT_EQUAL_STRING("address: missing, and no name to find the device by", error.c_str());
}

static void test_choices_list_the_allowed_names()
{
	std::string error;

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("oversize":"split")"), error));
	TEST_ASSERT_EQUAL_STRING("oversize: \"split\" is not one of chunk, long_write, drop", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("connect_policy":"never")"), error));
	TEST_ASSERT_EQUAL_STRING("connect_policy: \"never\" is not one of on_demand, always", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("tx":{"mode":"name"})"), error));
	TEST_ASSERT_EQUAL_STRING("tx.mode: \"name\" is not one of uuid, none", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("rx":{"subscribe":"both"})"), error));
	TEST_ASSERT_EQUAL_STRING("rx.subscribe: \"both\" is not one of auto, indicate, notify", error.c_str());

	TEST_ASSERT_TRUE_MESSAGE(ruleOk(ruleWith(R"("oversize":"long_write","connect_policy":"always","rx":{"subscribe":"notify"})"), error),
		error.c_str());
}

static void test_wrong_types_name_the_field()
{
	std::string error;

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("listen_port":65536)"), error));
	TEST_ASSERT_EQUAL_STRING("listen_port: expected a whole number from 0 to 65535", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("max_clients":-1)"), error));
	TEST_ASSERT_EQUAL_STRING("max_clients: expected a whole number from 0 to 255", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("idle_s":"3")"), error));
	TEST_ASSERT_EQUAL_STRING("idle_s: expected a whole number from 0 to 65535", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("coalesce_ms":2.5)"), error));
	TEST_ASSERT_EQUAL_STRING("coalesce_ms: expected a whole number from 0 to 65535", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("enabled":1)"), error));
	TEST_ASSERT_EQUAL_STRING("enabled: expected true or false", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("reconnect_backoff_s":5)"), error));
	TEST_ASSERT_EQUAL_STRING("reconnect_backoff_s: expected an array of whole numbers", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("reconnect_backoff_s":[1,"2"])"), error));
	TEST_ASSERT_EQUAL_STRING("reconnect_backoff_s: expected whole numbers from 0 to 65535", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("tx":"fed5")"), error));
	TEST_ASSERT_EQUAL_STRING("tx: expected an object", error.c_str());
}

static void test_texts_longer_than_their_block_are_refused()
{
	std::string error;

	TEST_ASSERT_FALSE(ruleOk(R"({"id":")" + std::string(33, 'a') + R"(","target":{"address":"12:34:56:78:9a:bc"},"service_uuid":"fee7"})", error));
	TEST_ASSERT_EQUAL_STRING("id: longer than 32 bytes", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("name":")" + std::string(64, 'n') + "\""), error));
	TEST_ASSERT_EQUAL_STRING("name: longer than 63 bytes", error.c_str());

	TEST_ASSERT_TRUE(ruleOk(ruleWith(R"("name":")" + std::string(63, 'n') + "\""), error));
}

static void test_ranges()
{
	std::string error;

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("listen_port":0)"), error));
	TEST_ASSERT_EQUAL_STRING("listen_port: 0 is out of range 1-65535", error.c_str());
	const std::string webPort = std::to_string(RuleConfig::kWebPort);
	TEST_ASSERT_FALSE(ruleOk(ruleWith("\"listen_port\":" + webPort), error));
	TEST_ASSERT_EQUAL_STRING(("listen_port: " + webPort + " is the web page's port").c_str(), error.c_str());
	TEST_ASSERT_TRUE(ruleOk(ruleWith(R"("listen_port":502)"), error));

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("max_clients":0)"), error));
	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("max_clients":5)"), error));
	TEST_ASSERT_EQUAL_STRING("max_clients: 5 is out of range 1-4", error.c_str());
	TEST_ASSERT_TRUE(ruleOk(ruleWith(R"("max_clients":4)"), error));


	TEST_ASSERT_TRUE(ruleOk(ruleWith(R"("coalesce_ms":0)"), error));
	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("coalesce_ms":1001)"), error));
	TEST_ASSERT_TRUE(ruleOk(ruleWith(R"("idle_s":0)"), error));
	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("idle_s":3601)"), error));

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("reconnect_backoff_s":[])"), error));
	TEST_ASSERT_EQUAL_STRING("reconnect_backoff_s: 1-10 steps", error.c_str());
	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("reconnect_backoff_s":[1,2,3,4,5,6,7,8,9,10,11])"), error));
	TEST_ASSERT_EQUAL_STRING("reconnect_backoff_s: at most 10 steps", error.c_str());
	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("reconnect_backoff_s":[1,0])"), error));
	TEST_ASSERT_EQUAL_STRING("reconnect_backoff_s: 0 is out of range 1-3600", error.c_str());
	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("reconnect_backoff_s":[3601])"), error));
	TEST_ASSERT_EQUAL_STRING("reconnect_backoff_s: 3601 is out of range 1-3600", error.c_str());
	TEST_ASSERT_TRUE(ruleOk(ruleWith(R"("reconnect_backoff_s":[3600])"), error));
}

static void test_channels_need_a_uuid_with_mode_uuid_only()
{
	std::string error;

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("tx":{"mode":"uuid"})"), error));
	TEST_ASSERT_EQUAL_STRING("tx.uuid: missing, mode uuid needs one, e.g. \"fed5\"", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("rx":{"mode":"auto"})"), error));
	TEST_ASSERT_EQUAL_STRING("rx.mode: \"auto\" is not one of uuid, none", error.c_str());

	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("tx":{"mode":"uuid","uuid":"fed"})"), error));
	TEST_ASSERT_EQUAL_STRING("tx.uuid: \"fed\" is not a GATT UUID, e.g. \"fed5\" or \"0000fed5-0000-1000-8000-00805f9b34fb\"", error.c_str());

	TEST_ASSERT_TRUE_MESSAGE(ruleOk(ruleWith(R"("tx":{"mode":"uuid","uuid":"0000FED5-0000-1000-8000-00805F9B34FB"})"), error), error.c_str());

	// none: a read-only or a write-only relay, not both
	TEST_ASSERT_TRUE_MESSAGE(ruleOk(ruleWith(R"("tx":{"mode":"none"})"), error), error.c_str());
	TEST_ASSERT_TRUE_MESSAGE(ruleOk(ruleWith(R"("rx":{"mode":"none"})"), error), error.c_str());
	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("tx":{"mode":"none","uuid":"fed5"})"), error));
	TEST_ASSERT_EQUAL_STRING("tx.uuid: must be empty with mode none", error.c_str());
	TEST_ASSERT_FALSE(ruleOk(ruleWith(R"("tx":{"mode":"none"},"rx":{"mode":"none"})"), error));
	TEST_ASSERT_EQUAL_STRING("rx.mode and tx.mode are None; a valid rule needs TX, RX or both", error.c_str());
	TEST_ASSERT_TRUE_MESSAGE(ruleOk(ruleWith(R"("rx":{"mode":"uuid","uuid":"0000fed6"})"), error), error.c_str());
}

static void test_id_and_label_rules()
{
	TEST_ASSERT_TRUE(w2b::isValidRuleId("inv1"));
	TEST_ASSERT_TRUE(w2b::isValidRuleId("Renac_inverter-2"));
	TEST_ASSERT_TRUE(w2b::isValidRuleId(std::string(32, 'a').c_str()));
	TEST_ASSERT_FALSE(w2b::isValidRuleId(std::string(33, 'a').c_str()));
	TEST_ASSERT_FALSE(w2b::isValidRuleId(""));
	TEST_ASSERT_FALSE(w2b::isValidRuleId("inv 1"));
	TEST_ASSERT_FALSE(w2b::isValidRuleId("inv/1"));

	TEST_ASSERT_TRUE(w2b::isValidLabel(""));
	TEST_ASSERT_TRUE(w2b::isValidLabel("Renac inverter, Küche"));
	TEST_ASSERT_FALSE(w2b::isValidLabel("tab\there"));
}

static void test_config_holds_the_rules_and_round_trips()
{
	ConfigData out = emptyConfig();
	std::string error;

	TEST_ASSERT_TRUE_MESSAGE(parseConfig(std::string(kPlanRule) + "," + minimalRule("b", "11:22:33:44:55:66", 9000), out, error), error.c_str());
	TEST_ASSERT_EQUAL(2, out.rules().size());
	TEST_ASSERT_EQUAL_STRING("inv1", out.rules()[0].id().c_str());
	TEST_ASSERT_EQUAL_UINT16(9000, out.findRule("b")->listenPort());
	TEST_ASSERT_NULL(out.findRule("c"));
	TEST_ASSERT_EQUAL(2, out.activeRules());

	ConfigData back = emptyConfig();
	TEST_ASSERT_TRUE_MESSAGE(JsonText::read(JsonText::write(out), back, error), error.c_str());
	TEST_ASSERT_TRUE(back == out);
}

static void test_rules_missing_keep_the_list_an_array_replaces_it()
{
	ConfigData out = emptyConfig();
	std::string error;

	TEST_ASSERT_TRUE_MESSAGE(parseConfig(kPlanRule, out, error), error.c_str());

	TEST_ASSERT_TRUE_MESSAGE(JsonText::read(R"({"schema":1,"device":{"hostname":"x"}})", out, error), error.c_str());
	TEST_ASSERT_EQUAL(1, out.rules().size());

	TEST_ASSERT_TRUE_MESSAGE(JsonText::read(R"({"schema":1,"rules":[]})", out, error), error.c_str());
	TEST_ASSERT_EQUAL(0, out.rules().size());
}

static void test_ids_are_unique_ports_among_the_enabled()
{
	ConfigData out = emptyConfig();
	std::string error;
	const std::string first = minimalRule("a", "12:34:56:78:9a:bc", 8899);

	TEST_ASSERT_FALSE(parseConfig(first + "," + minimalRule("a", "11:22:33:44:55:66", 9000, false), out, error));
	TEST_ASSERT_EQUAL_STRING("rules[1].id: \"a\" is the id of rules[0]", error.c_str());

	TEST_ASSERT_FALSE(parseConfig(first + "," + minimalRule("b", "11:22:33:44:55:66", 8899), out, error));
	TEST_ASSERT_EQUAL_STRING("rules[1].listen_port: 8899 is the port of enabled rule \"a\"", error.c_str());

	// untouched by the failed reads
	TEST_ASSERT_TRUE(out == emptyConfig());

	// a disabled rule may share the port
	TEST_ASSERT_TRUE_MESSAGE(parseConfig(first + "," + minimalRule("b", "12:34:56:78:9a:bc", 8899, false), out, error), error.c_str());
}

// Rules on different characteristics of one device, by address or by name.
static void test_enabled_rules_may_share_a_device()
{
	ConfigData out = emptyConfig();
	std::string error;
	const std::string addressed = R"({"id":"a","target":{"address":"12:34:56:78:9a:bc","name":"HF-LPT270"},"service_uuid":"fee7",)" + kTx + "}";

	TEST_ASSERT_TRUE_MESSAGE(parseConfig(addressed + "," + minimalRule("b", "12:34:56:78:9A:BC", 9000), out, error), error.c_str());
	TEST_ASSERT_TRUE_MESSAGE(parseConfig(addressed + "," + namedRule("b", "HF-LPT270", 9000), out, error), error.c_str());
	TEST_ASSERT_TRUE_MESSAGE(parseConfig(namedRule("a", "HF-LPT270", 8899) + "," + namedRule("b", "HF-LPT270", 9000), out, error), error.c_str());
	TEST_ASSERT_EQUAL(2, out.activeRules());
}

static void test_at_most_max_active_enabled_any_number_stored()
{
	ConfigData out = emptyConfig();
	std::string error;
	const size_t most = ConfigData::kMaxActiveRules;

	TEST_ASSERT_TRUE_MESSAGE(parseConfig(joined(most, 9000, true) + "," + minimalRule("off", "00:00:00:00:00:99", 9500, false), out, error),
		error.c_str());
	TEST_ASSERT_EQUAL(most, out.activeRules());

	TEST_ASSERT_FALSE(parseConfig(joined(most + 1, 9000, true), out, error));
	const std::string expected = "rules: " + std::to_string(most + 1) + " enabled, at most " + std::to_string(most) + " at a time";
	TEST_ASSERT_EQUAL_STRING(expected.c_str(), error.c_str());

	TEST_ASSERT_TRUE_MESSAGE(parseConfig(joined(50, 9000, false), out, error), error.c_str());
	TEST_ASSERT_EQUAL(50, out.rules().size());
}

static void test_rules_without_an_id_get_the_first_free_ones()
{
	ConfigData out = emptyConfig();
	std::string error;
	const std::string noId = R"({"target":{"address":"00:00:00:00:00:01"},"service_uuid":"fee7",)" + kTx + R"(,"enabled":false})";

	TEST_ASSERT_TRUE_MESSAGE(parseConfig(noId + "," + minimalRule("id0", "00:00:00:00:00:02", 9000, false) + "," + noId, out, error), error.c_str());
	TEST_ASSERT_EQUAL_STRING("id1", out.rules()[0].id().c_str());
	TEST_ASSERT_EQUAL_STRING("id0", out.rules()[1].id().c_str());
	TEST_ASSERT_EQUAL_STRING("id2", out.rules()[2].id().c_str());
}

static void test_rule_list_shape_is_checked()
{
	ConfigData out = emptyConfig();
	std::string error;

	TEST_ASSERT_FALSE(JsonText::read(R"({"schema":1,"rules":{"id":"a"}})", out, error));
	TEST_ASSERT_EQUAL_STRING("rules: expected an array", error.c_str());

	TEST_ASSERT_FALSE(parseConfig(minimalRule("a", "00:00:00:00:00:01", 9001) + ",7", out, error));
	TEST_ASSERT_EQUAL_STRING("rules[1]: expected an object", error.c_str());

	TEST_ASSERT_FALSE(parseConfig(R"({"id":"a","target":{"address":"x"},"service_uuid":"fee7"})", out, error));
	TEST_ASSERT_EQUAL_STRING("rules[0].target.address: \"x\" is not a BLE address, e.g. \"12:34:56:78:9a:bc\"", error.c_str());
}

static void test_constructor_sets_every_field()
{
	const RuleConfig built("inv1", "Renac inverter", true, 8899, 1, TargetConfig(address("12:34:56:78:9a:bc"), "HF-LPT270"),
		uuid("fee7"), TxConfig(ChannelMode::Uuid, uuid("fed5"), true), RxConfig(ChannelMode::Uuid, uuid("fed6"), RxConfig::Subscribe::Indicate), 10,
		RuleConfig::Oversize::Chunk, RuleConfig::ConnectPolicy::OnDemand, 3, {2, 4, 8, 10, 30}, false, false);
	RuleConfig parsed = SerializableFactory::createEmpty<RuleConfig>();
	std::string error;

	TEST_ASSERT_TRUE_MESSAGE(parseRule(kPlanRule, parsed, error), error.c_str());
	TEST_ASSERT_TRUE(built == parsed);
	TEST_ASSERT_TRUE_MESSAGE(built.validate(error), error.c_str());
}

static void test_backoff_steps_end_at_the_first_zero()
{
	const auto withBackoff = [](const RuleConfig::Backoff& backoff)
		{
			return RuleConfig("a", "", true, 8899, 1, TargetConfig(address("12:34:56:78:9a:bc"), ""), uuid("fee7"),
				TxConfig(ChannelMode::Uuid, uuid("fed5"), true), RxConfig(ChannelMode::None, BleUuid(), RxConfig::Subscribe::Auto), 10,
				RuleConfig::Oversize::Chunk, RuleConfig::ConnectPolicy::OnDemand, 3, backoff, false, false);
		};
	std::string error;

	TEST_ASSERT_EQUAL(10, withBackoff({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}).reconnectSteps());
	TEST_ASSERT_EQUAL(2, withBackoff({5, 7}).reconnectSteps());

	TEST_ASSERT_FALSE(withBackoff({1, 0, 5}).validate(error));
	TEST_ASSERT_EQUAL_STRING("reconnect_backoff_s: 0 is out of range 1-3600", error.c_str());

	TEST_ASSERT_FALSE(withBackoff({}).validate(error));
	TEST_ASSERT_EQUAL_STRING("reconnect_backoff_s: 1-10 steps", error.c_str());

	RuleConfig parsed = SerializableFactory::createEmpty<RuleConfig>();
	TEST_ASSERT_TRUE_MESSAGE(JsonText::read(JsonText::write(withBackoff({5, 7})), parsed, error), error.c_str());
	TEST_ASSERT_EQUAL(2, parsed.reconnectSteps());
	TEST_ASSERT_TRUE(parsed.reconnectBackoffS() == RuleConfig::Backoff({5, 7}));
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_the_hf_lpt270_rule_round_trips_key_for_key);
	RUN_TEST(test_required_keys_only_take_the_defaults);
	RUN_TEST(test_missing_required_keys_are_named);
	RUN_TEST(test_the_id_is_optional_and_the_target_an_address_or_a_name);
	RUN_TEST(test_choices_list_the_allowed_names);
	RUN_TEST(test_wrong_types_name_the_field);
	RUN_TEST(test_texts_longer_than_their_block_are_refused);
	RUN_TEST(test_ranges);
	RUN_TEST(test_channels_need_a_uuid_with_mode_uuid_only);
	RUN_TEST(test_id_and_label_rules);
	RUN_TEST(test_config_holds_the_rules_and_round_trips);
	RUN_TEST(test_rules_missing_keep_the_list_an_array_replaces_it);
	RUN_TEST(test_ids_are_unique_ports_among_the_enabled);
	RUN_TEST(test_enabled_rules_may_share_a_device);
	RUN_TEST(test_at_most_max_active_enabled_any_number_stored);
	RUN_TEST(test_rules_without_an_id_get_the_first_free_ones);
	RUN_TEST(test_rule_list_shape_is_checked);
	RUN_TEST(test_constructor_sets_every_field);
	RUN_TEST(test_backoff_steps_end_at_the_first_zero);

	return UNITY_END();
}
