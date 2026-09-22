#include <unity.h>

#include <optional>
#include <string>

#include "ConfigData.h"
#include "ConfigRules.h"
#include "IPv4Str.h"
#include "JsonText.h"

using w2b::ConfigData;
using w2b::DeviceConfig;
using w2b::JsonText;
using w2b::LogLevel;
using w2b::SerializableFactory;
using w2b::StaticIpConfig;
using w2b::WifiConfig;

namespace
{

const w2b::TimeZone& zone(const char* name)
{
	return *w2b::findTimeZone(name);
}

uint32_t ip(const char* text)
{
	uint32_t address = 0;
	w2b::parseIPv4(text, address);

	return address;
}

ConfigData dhcpConfig()
{
	return ConfigData(DeviceConfig("w2b", LogLevel::Info, zone("UTC"), false, DeviceConfig::kDefaultHeartbeatS,
		DeviceConfig::kUncalibratedBatteryScalePpm), WifiConfig("", "", std::nullopt), {});
}

ConfigData staticConfig()
{
	const StaticIpConfig address(ip("192.168.0.50"), ip("192.168.0.1"), ip("255.255.255.0"), ip("192.168.0.1"));

	return ConfigData(DeviceConfig("relay-2", LogLevel::Debug, zone("Europe/Berlin"), false, DeviceConfig::kDefaultHeartbeatS,
		DeviceConfig::kUncalibratedBatteryScalePpm), WifiConfig("Home Net", "correct horse", address), {});
}

// out starts as dhcpConfig(), so a failed read that leaves it untouched shows.
bool parse(const std::string& text, ConfigData& out, std::string& error)
{
	out = dhcpConfig();

	return JsonText::read(text, out, error);
}

bool contains(const std::string& text, const char* part)
{
	return text.find(part) != std::string::npos;
}

bool hostnameOk(const std::string& hostname)
{
	std::string error;

	return DeviceConfig(hostname.c_str(), LogLevel::Info, zone("UTC"), false, DeviceConfig::kDefaultHeartbeatS,
		DeviceConfig::kUncalibratedBatteryScalePpm).validate(error);
}

bool wifiOk(const std::string& ssid, const std::string& password)
{
	std::string error;

	return WifiConfig(ssid.c_str(), password.c_str(), std::nullopt).validate(error);
}

bool staticOk(const char* ipText, const char* gateway, const char* subnet, const char* dns, std::string& error)
{
	StaticIpConfig address = SerializableFactory::createEmpty<StaticIpConfig>();
	const std::string text = std::string("{\"ip\":\"") + ipText + "\",\"gateway\":\"" + gateway + "\",\"subnet\":\"" + subnet + "\",\"dns\":\"" + dns +
		"\"}";

	return JsonText::read(text, address, error);
}

}  // namespace

void setUp()
{
}

void tearDown()
{
}

static void test_serialized_layout()
{
	TEST_ASSERT_EQUAL_STRING("{\"schema\":1,\"device\":{\"hostname\":\"w2b\",\"log_level\":\"info\",\"timezone\":\"UTC\",\"use_random_ble_address\":false,\"heartbeat_s\":15,"
		"\"battery_scale_ppm\":1000000},\"wifi\":{\"ssid\":\"\",\"password\":\"\",\"static\":null},\"rules\":[]}",
		JsonText::write(dhcpConfig()).c_str());

	TEST_ASSERT_TRUE(contains(JsonText::write(staticConfig()),
		"\"static\":{\"ip\":\"192.168.0.50\",\"gateway\":\"192.168.0.1\",\"subnet\":\"255.255.255.0\",\"dns\":\"192.168.0.1\"}"));
}

static void test_round_trip_keeps_every_field()
{
	ConfigData out = dhcpConfig();
	std::string error;

	TEST_ASSERT_TRUE_MESSAGE(parse(JsonText::write(staticConfig()), out, error), error.c_str());
	TEST_ASSERT_TRUE(out == staticConfig());

	TEST_ASSERT_TRUE_MESSAGE(parse(JsonText::write(dhcpConfig()), out, error), error.c_str());
	TEST_ASSERT_TRUE(out == dhcpConfig());
	TEST_ASSERT_FALSE(out.wifi().staticIp().has_value());
}

static void test_default_config_is_valid_and_not_configured()
{
	std::string error;

	TEST_ASSERT_TRUE(ConfigData() == dhcpConfig());
	TEST_ASSERT_TRUE_MESSAGE(ConfigData().validate(error), error.c_str());
	TEST_ASSERT_TRUE(ConfigData().wifi().ssid().empty());
}

static void test_missing_keys_keep_the_current_values()
{
	ConfigData out = staticConfig();
	std::string error;

	TEST_ASSERT_TRUE_MESSAGE(JsonText::read("{\"schema\":1}", out, error), error.c_str());
	TEST_ASSERT_TRUE(out == staticConfig());

	TEST_ASSERT_TRUE_MESSAGE(JsonText::read("{\"schema\":1,\"device\":{\"log_level\":\"error\"},\"wifi\":{\"ssid\":\"Other\"}}", out, error), error.c_str());
	TEST_ASSERT_EQUAL_STRING("relay-2", out.device().hostname().c_str());
	TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::Error), static_cast<int>(out.device().logLevel()));
	TEST_ASSERT_EQUAL_STRING("Other", out.wifi().ssid().c_str());
	TEST_ASSERT_EQUAL_STRING("correct horse", out.wifi().password().c_str());

	// "static" missing: kept
	TEST_ASSERT_TRUE(out.wifi().staticIp() == staticConfig().wifi().staticIp());

	TEST_ASSERT_TRUE_MESSAGE(JsonText::read("{\"schema\":1,\"device\":null}", out, error), error.c_str());
	TEST_ASSERT_EQUAL_STRING("relay-2", out.device().hostname().c_str());
}

static void test_static_null_means_dhcp_and_its_own_keys_are_required()
{
	ConfigData out = staticConfig();
	std::string error;

	TEST_ASSERT_TRUE_MESSAGE(JsonText::read("{\"schema\":1,\"wifi\":{\"static\":null}}", out, error), error.c_str());
	TEST_ASSERT_FALSE(out.wifi().staticIp().has_value());

	TEST_ASSERT_FALSE(JsonText::read("{\"schema\":1,\"wifi\":{\"static\":{\"ip\":\"10.0.0.2\"}}}", out, error));
	TEST_ASSERT_EQUAL_STRING("wifi.static.gateway: missing", error.c_str());
	TEST_ASSERT_FALSE(out.wifi().staticIp().has_value());

	TEST_ASSERT_TRUE_MESSAGE(JsonText::read("{\"schema\":1,\"wifi\":{\"static\":{\"ip\":\"10.0.0.2\",\"gateway\":\"10.0.0.1\",\"subnet\":\"255.0.0.0\"}}}", out, error),
		error.c_str());
	TEST_ASSERT_EQUAL_UINT32(ip("10.0.0.2"), out.wifi().staticIp()->ip());

	// dns is optional
	TEST_ASSERT_EQUAL_UINT32(0, out.wifi().staticIp()->dns());
}

static void test_unknown_keys_are_ignored()
{
	ConfigData out = dhcpConfig();
	std::string error;
	const std::string text = "{\"schema\":1,\"bridges\":[{\"id\":\"inv1\"}],\"device\":{\"led_gpio\":22,\"hostname\":\"abc\",\"log_level\":\"warn\"},"
		"\"wifi\":{\"ssid\":\"\",\"password\":\"\",\"static\":null,\"band\":\"2.4\"}}";

	TEST_ASSERT_TRUE_MESSAGE(parse(text, out, error), error.c_str());
	TEST_ASSERT_EQUAL_STRING("abc", out.device().hostname().c_str());
	TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::Warn), static_cast<int>(out.device().logLevel()));
}

static void test_schema_is_required_and_must_match()
{
	ConfigData out = dhcpConfig();
	std::string error;

	TEST_ASSERT_FALSE(parse("{\"device\":{}}", out, error));
	TEST_ASSERT_EQUAL_STRING("schema: missing", error.c_str());

	TEST_ASSERT_FALSE(parse("{\"schema\":\"1\"}", out, error));
	TEST_ASSERT_EQUAL_STRING("schema: expected a whole number", error.c_str());

	TEST_ASSERT_FALSE(parse("{\"schema\":2}", out, error));
	TEST_ASSERT_EQUAL_STRING("schema: version 2 not supported, expected 1", error.c_str());
}

static void test_malformed_input_is_rejected()
{
	ConfigData out = dhcpConfig();
	std::string error;

	TEST_ASSERT_FALSE(parse("{\"schema\":1,", out, error));
	TEST_ASSERT_TRUE(contains(error, "not valid JSON"));

	TEST_ASSERT_FALSE(parse("", out, error));
	TEST_ASSERT_TRUE(contains(error, "not valid JSON"));

	TEST_ASSERT_FALSE(parse("[1,2]", out, error));
	TEST_ASSERT_EQUAL_STRING("not a JSON object", error.c_str());
}

static void test_wrong_types_name_the_field()
{
	ConfigData out = dhcpConfig();
	std::string error;

	TEST_ASSERT_FALSE(parse("{\"schema\":1,\"device\":{\"hostname\":5}}", out, error));
	TEST_ASSERT_EQUAL_STRING("device.hostname: expected a string", error.c_str());

	TEST_ASSERT_FALSE(parse("{\"schema\":1,\"device\":\"w2b\"}", out, error));
	TEST_ASSERT_EQUAL_STRING("device: expected an object", error.c_str());

	const char* prefix = "{\"schema\":1,\"device\":{\"hostname\":\"w2b\",\"log_level\":\"info\"},\"wifi\":{\"ssid\":\"\",\"password\":\"\",\"static\":";

	TEST_ASSERT_FALSE(parse(std::string(prefix) + "true}}", out, error));
	TEST_ASSERT_EQUAL_STRING("wifi.static: expected an object", error.c_str());

	TEST_ASSERT_FALSE(parse(std::string(prefix) + "{\"ip\":1}}}", out, error));
	TEST_ASSERT_EQUAL_STRING("wifi.static.ip: expected a string", error.c_str());
}

static void test_texts_longer_than_their_block_are_refused()
{
	ConfigData out = dhcpConfig();
	std::string error;

	TEST_ASSERT_FALSE(parse("{\"schema\":1,\"device\":{\"hostname\":\"" + std::string(64, 'a') + "\"}}", out, error));
	TEST_ASSERT_EQUAL_STRING("device.hostname: longer than 63 bytes", error.c_str());

	TEST_ASSERT_FALSE(parse("{\"schema\":1,\"wifi\":{\"ssid\":\"" + std::string(33, 's') + "\"}}", out, error));
	TEST_ASSERT_EQUAL_STRING("wifi.ssid: longer than 32 bytes", error.c_str());

	TEST_ASSERT_TRUE(out == dhcpConfig());
}

static void test_unknown_log_level_is_rejected()
{
	ConfigData out = dhcpConfig();
	std::string error;

	TEST_ASSERT_FALSE(parse("{\"schema\":1,\"device\":{\"hostname\":\"w2b\",\"log_level\":\"verbose\"}}", out, error));
	TEST_ASSERT_EQUAL_STRING("device.log_level: \"verbose\" is not one of error, warn, info, debug", error.c_str());
}

static void test_timezone_must_be_a_known_zone_and_is_kept_when_missing()
{
	ConfigData out = dhcpConfig();
	std::string error;

	TEST_ASSERT_FALSE(parse("{\"schema\":1,\"device\":{\"timezone\":\"Mars/Olympus\"}}", out, error));
	TEST_ASSERT_EQUAL_STRING("device.timezone: \"Mars/Olympus\" is not a known time zone name, e.g. \"Europe/Berlin\" or \"UTC\"", error.c_str());

	TEST_ASSERT_FALSE(parse("{\"schema\":1,\"device\":{\"timezone\":\"europe/berlin\"}}", out, error));

	TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema\":1,\"device\":{\"timezone\":\"America/New_York\"}}", out, error), error.c_str());
	TEST_ASSERT_EQUAL_STRING("America/New_York", out.device().timeZone().name());

	// an older file without the key keeps the zone it had
	out = staticConfig();
	TEST_ASSERT_TRUE_MESSAGE(JsonText::read("{\"schema\":1,\"device\":{\"hostname\":\"x\",\"log_level\":\"info\"}}", out, error), error.c_str());
	TEST_ASSERT_EQUAL_STRING("Europe/Berlin", out.device().timeZone().name());
}

static void test_random_ble_address_is_a_bool_and_kept_when_missing()
{
	ConfigData out = dhcpConfig();
	std::string error;

	TEST_ASSERT_FALSE(out.device().useRandomBleAddress());
	TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema\":1,\"device\":{\"use_random_ble_address\":true}}", out, error), error.c_str());
	TEST_ASSERT_TRUE(out.device().useRandomBleAddress());

	// a file without the key keeps the choice it had
	TEST_ASSERT_TRUE_MESSAGE(JsonText::read("{\"schema\":1,\"device\":{\"hostname\":\"x\"}}", out, error), error.c_str());
	TEST_ASSERT_TRUE(out.device().useRandomBleAddress());

	TEST_ASSERT_FALSE(parse("{\"schema\":1,\"device\":{\"use_random_ble_address\":\"yes\"}}", out, error));
	TEST_ASSERT_EQUAL_STRING("device.use_random_ble_address: expected true or false", error.c_str());
}

static void test_heartbeat_period_is_kept_and_limited()
{
	ConfigData out = dhcpConfig();
	std::string error;

	TEST_ASSERT_EQUAL(DeviceConfig::kDefaultHeartbeatS, out.device().heartbeatS());
	TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema\":1,\"device\":{\"heartbeat_s\":0}}", out, error), error.c_str());
	TEST_ASSERT_EQUAL(0, out.device().heartbeatS());

	// a file without the key keeps the period it had
	TEST_ASSERT_TRUE_MESSAGE(JsonText::read("{\"schema\":1,\"device\":{\"hostname\":\"x\"}}", out, error), error.c_str());
	TEST_ASSERT_EQUAL(0, out.device().heartbeatS());

	TEST_ASSERT_FALSE(parse("{\"schema\":1,\"device\":{\"heartbeat_s\":3601}}", out, error));
	TEST_ASSERT_EQUAL_STRING("device.heartbeat_s: 3601 is above 3600", error.c_str());

	TEST_ASSERT_FALSE(parse("{\"schema\":1,\"device\":{\"heartbeat_s\":\"often\"}}", out, error));
	TEST_ASSERT_EQUAL_STRING("device.heartbeat_s: expected a whole number from 0 to 65535", error.c_str());
}

static void test_battery_scale_is_kept_and_limited()
{
	ConfigData out = dhcpConfig();
	std::string error;

	TEST_ASSERT_EQUAL(DeviceConfig::kUncalibratedBatteryScalePpm, out.device().batteryScalePpm());
	TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema\":1,\"device\":{\"battery_scale_ppm\":990000}}", out, error), error.c_str());
	TEST_ASSERT_EQUAL(990000, out.device().batteryScalePpm());

	// a file without the key keeps the scale it had
	TEST_ASSERT_TRUE_MESSAGE(JsonText::read("{\"schema\":1,\"device\":{\"hostname\":\"x\"}}", out, error), error.c_str());
	TEST_ASSERT_EQUAL(990000, out.device().batteryScalePpm());

	TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema\":1,\"device\":{\"battery_scale_ppm\":1100000}}", out, error), error.c_str());
	TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema\":1,\"device\":{\"battery_scale_ppm\":900000}}", out, error), error.c_str());

	TEST_ASSERT_FALSE(parse("{\"schema\":1,\"device\":{\"battery_scale_ppm\":1100001}}", out, error));
	TEST_ASSERT_EQUAL_STRING("device.battery_scale_ppm: 1100001 is more than 100000 from 1000000", error.c_str());

	TEST_ASSERT_FALSE(parse("{\"schema\":1,\"device\":{\"battery_scale_ppm\":899999}}", out, error));
	TEST_ASSERT_EQUAL_STRING("device.battery_scale_ppm: 899999 is more than 100000 from 1000000", error.c_str());
}

static void test_invalid_values_fail_with_their_path_and_leave_out_untouched()
{
	ConfigData out = dhcpConfig();
	std::string error;
	const std::string text = JsonText::write(staticConfig());

	std::string badHost = text;
	badHost.replace(badHost.find("relay-2"), 7, "-relay2");
	TEST_ASSERT_FALSE(parse(badHost, out, error));
	TEST_ASSERT_TRUE(contains(error, "device.hostname: \"-relay2\""));
	TEST_ASSERT_TRUE(out == dhcpConfig());

	std::string badGateway = text;
	badGateway.replace(badGateway.find("\"192.168.0.1\""), 13, "\"192.168.1.1\"");
	TEST_ASSERT_FALSE(parse(badGateway, out, error));
	TEST_ASSERT_EQUAL_STRING("wifi.static.gateway: must be another address in the subnet of ip", error.c_str());
	TEST_ASSERT_TRUE(out == dhcpConfig());
}

static void test_validate_prefixes_the_section()
{
	std::string error;
	const StaticIpConfig badMask(ip("10.0.0.2"), ip("10.0.0.1"), ip("255.0.255.0"), 0);

	TEST_ASSERT_FALSE(ConfigData(DeviceConfig("-x", LogLevel::Info, zone("UTC"), false, DeviceConfig::kDefaultHeartbeatS,
		DeviceConfig::kUncalibratedBatteryScalePpm), WifiConfig("", "", std::nullopt), {}).validate(error));
	TEST_ASSERT_TRUE(contains(error, "device.hostname: "));

	TEST_ASSERT_FALSE(ConfigData(DeviceConfig("w2b", LogLevel::Info, zone("UTC"), false, DeviceConfig::kDefaultHeartbeatS,
		DeviceConfig::kUncalibratedBatteryScalePpm), WifiConfig("", "", badMask), {}).validate(error));
	TEST_ASSERT_EQUAL_STRING("wifi.static.subnet: \"255.0.255.0\" is not a subnet mask", error.c_str());

	TEST_ASSERT_TRUE(staticConfig().validate(error));
	TEST_ASSERT_TRUE(dhcpConfig().validate(error));
}

static void test_hostname_rules()
{
	TEST_ASSERT_TRUE(hostnameOk("w2b"));
	TEST_ASSERT_TRUE(hostnameOk("A1-b2"));
	TEST_ASSERT_TRUE(hostnameOk(std::string(63, 'a')));
	TEST_ASSERT_FALSE(hostnameOk(std::string(64, 'a')));
	TEST_ASSERT_FALSE(hostnameOk(""));
	TEST_ASSERT_FALSE(hostnameOk("-w2b"));
	TEST_ASSERT_FALSE(hostnameOk("w2b-"));
	TEST_ASSERT_FALSE(hostnameOk("w2b_relay"));
	TEST_ASSERT_FALSE(hostnameOk("w2b.local"));
}

static void test_ssid_and_password_rules()
{
	TEST_ASSERT_TRUE(wifiOk("", ""));
	TEST_ASSERT_TRUE(wifiOk(std::string(32, 's'), ""));
	TEST_ASSERT_FALSE(wifiOk(std::string(33, 's'), ""));

	TEST_ASSERT_TRUE(wifiOk("home", "12345678"));
	TEST_ASSERT_TRUE(wifiOk("home", std::string(63, '~')));
	TEST_ASSERT_TRUE(wifiOk("home", "0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF"));
	TEST_ASSERT_FALSE(wifiOk("home", "1234567"));
	TEST_ASSERT_FALSE(wifiOk("home", std::string(64, 'g')));
	TEST_ASSERT_FALSE(wifiOk("home", std::string(65, 'a')));
	TEST_ASSERT_FALSE(wifiOk("home", "tab\there!"));
}

static void test_static_address_rules()
{
	std::string error;

	TEST_ASSERT_TRUE(staticOk("192.168.0.50", "192.168.0.1", "255.255.255.0", "", error));
	TEST_ASSERT_TRUE(staticOk("10.1.2.3", "10.0.0.1", "255.0.0.0", "8.8.8.8", error));

	TEST_ASSERT_FALSE(staticOk("256.1.1.1", "192.168.0.1", "255.255.255.0", "", error));
	TEST_ASSERT_EQUAL_STRING("ip: \"256.1.1.1\" is not an IPv4 address", error.c_str());
	TEST_ASSERT_FALSE(staticOk("192.168.0", "192.168.0.1", "255.255.255.0", "", error));
	TEST_ASSERT_FALSE(staticOk("192.168.0.5000", "192.168.0.1", "255.255.255.0", "", error));
	TEST_ASSERT_FALSE(staticOk("192.168..50", "192.168.0.1", "255.255.255.0", "", error));
	TEST_ASSERT_FALSE(staticOk(" 192.168.0.50", "192.168.0.1", "255.255.255.0", "", error));
	TEST_ASSERT_FALSE(staticOk("192.168.0.50", "192.168.0.1", "255.255.255.0", "dns.local", error));
	TEST_ASSERT_TRUE(contains(error, "dns: "));

	TEST_ASSERT_FALSE(staticOk("192.168.0.50", "192.168.0.1", "0.0.0.0", "", error));
	TEST_ASSERT_TRUE(contains(error, "is not a subnet mask"));
	TEST_ASSERT_FALSE(staticOk("192.168.0.50", "192.168.1.1", "255.255.255.0", "", error));
	TEST_ASSERT_FALSE(staticOk("192.168.0.50", "192.168.0.50", "255.255.255.0", "", error));
	TEST_ASSERT_TRUE(contains(error, "gateway: "));
}

static void test_ipv4_text_round_trips()
{
	TEST_ASSERT_EQUAL_UINT32(0xc0a80001, ip("192.168.0.1"));
	TEST_ASSERT_EQUAL_STRING("192.168.0.1", w2b::IPv4Str(0xc0a80001).c_str());
	TEST_ASSERT_EQUAL_STRING("255.255.255.255", w2b::IPv4Str(0xffffffff).c_str());
	TEST_ASSERT_EQUAL_STRING("0.0.0.0", w2b::IPv4Str(0).c_str());
	TEST_ASSERT_EQUAL_STRING("10.0.100.7", w2b::IPv4Str(0x0a006407).c_str());
	TEST_ASSERT_EQUAL_STRING("1.20.199.250", w2b::IPv4Str(0x0114c7fa).c_str());
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_serialized_layout);
	RUN_TEST(test_round_trip_keeps_every_field);
	RUN_TEST(test_default_config_is_valid_and_not_configured);
	RUN_TEST(test_missing_keys_keep_the_current_values);
	RUN_TEST(test_static_null_means_dhcp_and_its_own_keys_are_required);
	RUN_TEST(test_unknown_keys_are_ignored);
	RUN_TEST(test_schema_is_required_and_must_match);
	RUN_TEST(test_malformed_input_is_rejected);
	RUN_TEST(test_wrong_types_name_the_field);
	RUN_TEST(test_texts_longer_than_their_block_are_refused);
	RUN_TEST(test_unknown_log_level_is_rejected);
	RUN_TEST(test_heartbeat_period_is_kept_and_limited);
	RUN_TEST(test_battery_scale_is_kept_and_limited);
	RUN_TEST(test_timezone_must_be_a_known_zone_and_is_kept_when_missing);
	RUN_TEST(test_random_ble_address_is_a_bool_and_kept_when_missing);
	RUN_TEST(test_invalid_values_fail_with_their_path_and_leave_out_untouched);
	RUN_TEST(test_validate_prefixes_the_section);
	RUN_TEST(test_hostname_rules);
	RUN_TEST(test_ssid_and_password_rules);
	RUN_TEST(test_static_address_rules);
	RUN_TEST(test_ipv4_text_round_trips);

	return UNITY_END();
}
