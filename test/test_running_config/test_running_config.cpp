#include <unity.h>

#include <optional>
#include <string>

#include "Config.h"
#include "JsonSerializable.h"
#include "JsonText.h"
#include "Lock.h"
#include "LogRing.h"
#include "LogStamp.h"
#include "Logger.h"

using w2b::Config;
using w2b::ConfigData;
using w2b::DeviceConfig;
using w2b::LogLevel;
using w2b::RuleConfig;
using w2b::SerializableFactory;
using w2b::WifiConfig;
using ChangeResult = w2b::Config::ChangeResult;
using SaveState = w2b::Config::SaveState;

namespace
{

// Counts how deep it is held, so a test sees whether a callback runs under it.
struct CountingLock : w2b::Lock
{
	int depth = 0;

	void lock() override
	{
		++depth;
	}

	void unlock() override
	{
		--depth;
	}
};

size_t zeroStamp(char* out, size_t cap)
{
	return w2b::formatUptimeStamp(0, out, cap);
}

char logStorage[4096];
w2b::LogRing logRing(logStorage, sizeof(logStorage));
w2b::Logger logger(logRing, zeroStamp, nullptr);

CountingLock lock;
int saves = 0;
bool saveWorks = true;
ConfigData lastSaved;

bool fakeSave(const ConfigData& config)
{
	++saves;
	lastSaved = config;

	return saveWorks;
}

const w2b::TimeZone& zone(const char* name)
{
	return *w2b::findTimeZone(name);
}

ConfigData homeConfig()
{
	return ConfigData(DeviceConfig("relay", LogLevel::Info, zone("UTC"), false, DeviceConfig::kDefaultHeartbeatS,
		DeviceConfig::kUncalibratedBatteryScalePpm), WifiConfig("Home", "secret123", std::nullopt), {});
}

void loadHome(Config& config)
{
	config.load([](ConfigData& data)
		{
			data = homeConfig();
		});
}

Config::Edit zoneEdit(const char* name)
{
	return [name](ConfigData& newConfig, std::string& error)
	{
		const w2b::TimeZone* found = w2b::findTimeZone(name);

		if (found == nullptr)
		{
			error = "device.timezone: unknown";
			return false;
		}

		const DeviceConfig& device = newConfig.device();
		newConfig.setDevice(
			DeviceConfig(device.hostname(), device.logLevel(), *found, device.useRandomBleAddress(), device.heartbeatS(), device.batteryScalePpm()));

		return true;
	};
}

// a rule with the id "given", which addRule() does not use
RuleConfig rule(const char* address, uint16_t port, bool enabled)
{
	RuleConfig made = SerializableFactory::createEmpty<RuleConfig>();
	std::string error;
	const std::string json = std::string("{\"id\":\"given\",\"target\":{\"address\":\"") + address + "\"},\"service_uuid\":\"fee7\","
		"\"tx\":{\"mode\":\"uuid\",\"uuid\":\"fed5\"},\"listen_port\":" + std::to_string(port) + ",\"enabled\":" + (enabled ? "true" : "false") + "}";

	TEST_ASSERT_TRUE_MESSAGE(w2b::JsonText::read(json, made, error), error.c_str());

	return made;
}

}  // namespace

void setUp()
{
	logRing.clear();
	saves = 0;
	saveWorks = true;
}

void tearDown()
{
}

static void test_a_change_is_edited_at_once_and_saved_by_update()
{
	Config config(lock, fakeSave);
	std::string error;
	loadHome(config);

	TEST_ASSERT_EQUAL(static_cast<int>(ChangeResult::Invalid), static_cast<int>(config.submit("bad zone", zoneEdit("Mars/Olympus"), nullptr, error)));
	TEST_ASSERT_EQUAL_STRING("device.timezone: unknown", error.c_str());
	TEST_ASSERT_EQUAL(static_cast<int>(SaveState::Idle), static_cast<int>(config.saveState()));

	TEST_ASSERT_EQUAL(static_cast<int>(ChangeResult::Accepted), static_cast<int>(config.submit("zone", zoneEdit("Europe/Berlin"), nullptr, error)));
	TEST_ASSERT_EQUAL(static_cast<int>(SaveState::Pending), static_cast<int>(config.saveState()));

	// nothing changes before update()
	TEST_ASSERT_EQUAL_STRING("UTC", config.device().timeZone().name());
	TEST_ASSERT_EQUAL_INT(0, saves);

	config.update(100);

	TEST_ASSERT_EQUAL(static_cast<int>(SaveState::Saved), static_cast<int>(config.saveState()));
	TEST_ASSERT_EQUAL_STRING("Europe/Berlin", config.device().timeZone().name());
	TEST_ASSERT_EQUAL_INT(1, saves);
	TEST_ASSERT_EQUAL_STRING("Europe/Berlin", lastSaved.device().timeZone().name());
	TEST_ASSERT_EQUAL_STRING("Home", lastSaved.wifi().ssid().c_str());
	TEST_ASSERT_EQUAL_INT(0, lock.depth);
}

static void test_an_invalid_result_is_refused_with_its_path()
{
	Config config(lock, fakeSave);
	std::string error;
	loadHome(config);

	auto badPassword = [](ConfigData& newConfig, std::string&)
	{
		newConfig.setWifi(WifiConfig("Home", "short", std::nullopt));
		return true;
	};

	TEST_ASSERT_EQUAL(static_cast<int>(ChangeResult::Invalid), static_cast<int>(config.submit("password", badPassword, nullptr, error)));
	TEST_ASSERT_EQUAL_STRING("wifi.password: must be empty, 8-63 printable ASCII characters, or 64 hex digits", error.c_str());
	TEST_ASSERT_EQUAL_STRING("secret123", config.wifi().password().c_str());

	TEST_ASSERT_EQUAL(static_cast<int>(ChangeResult::Invalid), static_cast<int>(config.submit("nothing", nullptr, nullptr, error)));
	TEST_ASSERT_EQUAL_STRING("no change given", error.c_str());
}

static void test_one_change_at_a_time()
{
	Config config(lock, fakeSave);
	std::string error;
	loadHome(config);

	TEST_ASSERT_EQUAL(static_cast<int>(ChangeResult::Accepted), static_cast<int>(config.submit("first", zoneEdit("Europe/Berlin"), nullptr, error)));
	TEST_ASSERT_EQUAL(static_cast<int>(ChangeResult::Busy), static_cast<int>(config.submit("second", zoneEdit("Asia/Tokyo"), nullptr, error)));
	TEST_ASSERT_EQUAL_STRING("another configuration change is being saved", error.c_str());

	config.update(100);
	TEST_ASSERT_EQUAL_STRING("Europe/Berlin", config.device().timeZone().name());

	TEST_ASSERT_EQUAL(static_cast<int>(ChangeResult::Accepted), static_cast<int>(config.submit("third", zoneEdit("Asia/Tokyo"), nullptr, error)));
	config.update(100);
	TEST_ASSERT_EQUAL_STRING("Asia/Tokyo", config.device().timeZone().name());
}

static void test_a_failed_save_keeps_the_running_config_and_skips_the_follow_up()
{
	Config config(lock, fakeSave);
	std::string error;
	int followUps = 0;
	loadHome(config);
	saveWorks = false;

	auto count = [&followUps]
	{
		++followUps;
	};

	TEST_ASSERT_EQUAL(static_cast<int>(ChangeResult::Accepted), static_cast<int>(config.submit("zone", zoneEdit("Europe/Berlin"), count, error)));
	config.update(100);

	TEST_ASSERT_EQUAL(static_cast<int>(SaveState::Failed), static_cast<int>(config.saveState()));
	TEST_ASSERT_EQUAL_STRING("UTC", config.device().timeZone().name());
	TEST_ASSERT_EQUAL_INT(0, followUps);
}

// The follow-up (a restart, a time zone) sees the new configuration in use, runs without the lock, and before "saved".
static void test_the_follow_up_runs_after_the_save_without_the_lock_before_saved()
{
	Config config(lock, fakeSave);
	std::string error;
	std::string zoneSeen;
	int depthSeen = -1;
	SaveState stateSeen = SaveState::Idle;
	loadHome(config);

	auto followUp = [&]
	{
		zoneSeen = config.device().timeZone().name();
		depthSeen = lock.depth;
		stateSeen = config.saveState();
	};

	TEST_ASSERT_EQUAL(static_cast<int>(ChangeResult::Accepted), static_cast<int>(config.submit("zone", zoneEdit("Europe/Berlin"), followUp, error)));
	config.update(100);

	TEST_ASSERT_EQUAL_STRING("Europe/Berlin", zoneSeen.c_str());
	TEST_ASSERT_EQUAL_INT(0, depthSeen);
	TEST_ASSERT_EQUAL(static_cast<int>(SaveState::Pending), static_cast<int>(stateSeen));
	TEST_ASSERT_EQUAL(static_cast<int>(SaveState::Saved), static_cast<int>(config.saveState()));
}

static void test_reads_run_under_the_lock_and_hand_out_copies()
{
	Config config(lock, fakeSave);
	std::string error;
	int depthSeen = -1;
	loadHome(config);

	auto addRule = [](ConfigData& newConfig, std::string&)
	{
		newConfig.addRule(rule("12:34:56:78:9a:bc", 8899, true));
		return true;
	};

	TEST_ASSERT_EQUAL(static_cast<int>(ChangeResult::Accepted), static_cast<int>(config.submit("rule", addRule, nullptr, error)));
	config.update(100);

	RuleConfig found = SerializableFactory::createEmpty<RuleConfig>();
	TEST_ASSERT_TRUE(config.findRule("id0", found));
	TEST_ASSERT_EQUAL_UINT16(8899, found.listenPort());
	TEST_ASSERT_FALSE(config.findRule("id1", found));

	config.read([&depthSeen](const ConfigData& data)
		{
			depthSeen = lock.depth;
			TEST_ASSERT_EQUAL(1, data.rules().size());
		});

	TEST_ASSERT_EQUAL_INT(1, depthSeen);
	TEST_ASSERT_EQUAL_INT(0, lock.depth);
}

static void test_edits_start_from_the_running_config()
{
	Config config(lock, fakeSave);
	std::string error;
	loadHome(config);

	TEST_ASSERT_EQUAL(static_cast<int>(ChangeResult::Accepted), static_cast<int>(config.submit("zone", zoneEdit("Europe/Berlin"), nullptr, error)));
	config.update(100);

	auto addRule = [](ConfigData& newConfig, std::string&)
	{
		newConfig.addRule(rule("00:00:00:00:00:01", 9001, true));
		return true;
	};

	TEST_ASSERT_EQUAL(static_cast<int>(ChangeResult::Accepted), static_cast<int>(config.submit("rule", addRule, nullptr, error)));
	config.update(100);

	TEST_ASSERT_EQUAL_STRING("Europe/Berlin", lastSaved.device().timeZone().name());
	TEST_ASSERT_EQUAL(1, lastSaved.rules().size());
}

static void test_the_revision_counts_the_configurations_put_in_use()
{
	Config config(lock, fakeSave);
	std::string error;
	TEST_ASSERT_EQUAL_UINT32(0, config.currentRevision());

	loadHome(config);
	TEST_ASSERT_EQUAL_UINT32(1, config.currentRevision());

	// submitted but not saved yet, then saved
	config.submit("zone", zoneEdit("Europe/Berlin"), nullptr, error);
	TEST_ASSERT_EQUAL_UINT32(1, config.currentRevision());
	config.update(100);
	TEST_ASSERT_EQUAL_UINT32(2, config.currentRevision());

	// a failed save puts nothing in use
	saveWorks = false;
	config.submit("zone", zoneEdit("Europe/Paris"), nullptr, error);
	config.update(100);
	TEST_ASSERT_EQUAL_UINT32(2, config.currentRevision());
}

static void test_rule_list_edits()
{
	ConfigData data = homeConfig();
	std::string error;

	TEST_ASSERT_EQUAL_STRING("id0", data.addRule(rule("00:00:00:00:00:01", 9001, true)).c_str());
	TEST_ASSERT_EQUAL_STRING("id1", data.addRule(rule("00:00:00:00:00:02", 9002, false)).c_str());
	TEST_ASSERT_NULL(data.findRule("given"));

	TEST_ASSERT_TRUE(data.replaceRule("id0", rule("00:00:00:00:00:09", 9009, true), error));
	TEST_ASSERT_EQUAL_UINT16(9009, data.rules()[0].listenPort());
	TEST_ASSERT_EQUAL_STRING("id0", data.rules()[0].id().c_str());
	TEST_ASSERT_FALSE(data.replaceRule("id7", rule("00:00:00:00:00:09", 9009, true), error));
	TEST_ASSERT_EQUAL_STRING("no rule \"id7\"", error.c_str());

	TEST_ASSERT_TRUE(data.removeRule("id0", error));
	TEST_ASSERT_EQUAL(1, data.rules().size());
	TEST_ASSERT_EQUAL_STRING("id1", data.rules()[0].id().c_str());
	TEST_ASSERT_FALSE(data.removeRule("id0", error));

	// the first free number: id0 again, then id2
	TEST_ASSERT_EQUAL_STRING("id0", data.addRule(rule("00:00:00:00:00:03", 9003, false)).c_str());
	TEST_ASSERT_EQUAL_STRING("id2", data.addRule(rule("00:00:00:00:00:04", 9004, false)).c_str());

	for (size_t i = 3; i < 30; ++i)
	{
		data.addRule(rule("00:00:00:00:00:02", static_cast<uint16_t>(9100 + i), false));
	}

	TEST_ASSERT_EQUAL(30, data.rules().size());
	TEST_ASSERT_TRUE_MESSAGE(data.validate(error), error.c_str());
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_a_change_is_edited_at_once_and_saved_by_update);
	RUN_TEST(test_an_invalid_result_is_refused_with_its_path);
	RUN_TEST(test_one_change_at_a_time);
	RUN_TEST(test_a_failed_save_keeps_the_running_config_and_skips_the_follow_up);
	RUN_TEST(test_the_follow_up_runs_after_the_save_without_the_lock_before_saved);
	RUN_TEST(test_reads_run_under_the_lock_and_hand_out_copies);
	RUN_TEST(test_edits_start_from_the_running_config);
	RUN_TEST(test_the_revision_counts_the_configurations_put_in_use);
	RUN_TEST(test_rule_list_edits);

	return UNITY_END();
}
