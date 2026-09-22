#include <unity.h>

#include <cstring>
#include <map>
#include <optional>
#include <string>

#include "ConfigStore.h"
#include "FileSystem.h"
#include "LogRing.h"
#include "JsonText.h"
#include "LogStamp.h"
#include "Logger.h"

using w2b::ConfigData;
using w2b::ConfigStore;
using w2b::DeviceConfig;
using w2b::JsonText;
using w2b::LogLevel;
using w2b::RuleConfig;
using w2b::SerializableFactory;
using w2b::WifiConfig;
using Result = w2b::ConfigStore::LoadResult;

namespace
{

// Whole files in a map. The switches imitate file systems that refuse to rename over a file, fail writes, or corrupt them silently.
struct FakeFileSystem : w2b::FileSystem
{
	std::map<std::string, std::string> files;

	bool renameReplaces = true;

	bool renameWorks = true;

	bool writeWorks = true;

	bool writeTruncates = false;

	int writes = 0;

	bool exists(const char* path) const override
	{
		return files.count(path) > 0;
	}

	bool read(const char* path, size_t maxBytes, std::string& out) const override
	{
		auto found = files.find(path);

		if (found == files.end() || found->second.size() > maxBytes)
		{
			return false;
		}

		out = found->second;

		return true;
	}

	bool write(const char* path, const std::string& data) override
	{
		++writes;

		if (!writeWorks)
		{
			return false;
		}

		files[path] = writeTruncates ? data.substr(0, data.size() / 2) : data;

		return true;
	}

	bool rename(const char* from, const char* to) override
	{
		auto found = files.find(from);

		if (!renameWorks || found == files.end() || (!renameReplaces && exists(to)))
		{
			return false;
		}

		std::string data = found->second;
		files.erase(found);
		files[to] = data;

		return true;
	}

	bool remove(const char* path) override
	{
		return files.erase(path) > 0;
	}
};

size_t zeroStamp(char* out, size_t cap)
{
	return w2b::formatUptimeStamp(0, out, cap);
}

char logStorage[4096];
w2b::LogRing logRing(logStorage, sizeof(logStorage));
w2b::Logger logger(logRing, zeroStamp, nullptr);

const w2b::TimeZone& utc()
{
	return w2b::defaultTimeZone();
}

DeviceConfig defaultDevice()
{
	return DeviceConfig("w2b", LogLevel::Info, utc(), false, DeviceConfig::kDefaultHeartbeatS,
		DeviceConfig::kUncalibratedBatteryScalePpm);
}

ConfigData defaults()
{
	return ConfigData(defaultDevice(), WifiConfig("", "", std::nullopt), {});
}

ConfigData homeConfig()
{
	return ConfigData(DeviceConfig("relay", LogLevel::Info, utc(), false, DeviceConfig::kDefaultHeartbeatS,
		DeviceConfig::kUncalibratedBatteryScalePpm), WifiConfig("Home", "secret123", std::nullopt), {});
}

std::string defaultsJson()
{
	return JsonText::write(defaults());
}

std::string homeJson()
{
	return JsonText::write(homeConfig());
}

// a disabled rule with a 63-byte name
RuleConfig longRule()
{
	const std::string json = R"({"name":")" + std::string(63, 'n') + R"(","enabled":false,"target":{"address":"00:00:00:00:00:01"},"service_uuid":"fee7",)"
		R"("tx":{"mode":"uuid","uuid":"fed5"}})";
	RuleConfig rule = SerializableFactory::createEmpty<RuleConfig>();
	std::string error;

	TEST_ASSERT_TRUE_MESSAGE(JsonText::read(json, rule, error), error.c_str());

	return rule;
}

}  // namespace

void setUp()
{
	logRing.clear();
}

void tearDown()
{
}

static void test_missing_file_writes_the_defaults()
{
	FakeFileSystem fs;
	ConfigStore store(fs);
	ConfigData out = homeConfig();

	TEST_ASSERT_EQUAL(static_cast<int>(Result::Created), static_cast<int>(store.load(defaultDevice(), out)));
	TEST_ASSERT_TRUE(out == defaults());
	TEST_ASSERT_EQUAL_STRING(defaultsJson().c_str(), fs.files[ConfigStore::kPath].c_str());
	TEST_ASSERT_FALSE(fs.exists(ConfigStore::kTempPath));
}

static void test_valid_file_loads_without_writing()
{
	FakeFileSystem fs;
	fs.files[ConfigStore::kPath] = homeJson();
	ConfigStore store(fs);
	ConfigData out = defaults();

	TEST_ASSERT_EQUAL(static_cast<int>(Result::Loaded), static_cast<int>(store.load(defaultDevice(), out)));
	TEST_ASSERT_TRUE(out == homeConfig());
	TEST_ASSERT_EQUAL_INT(0, fs.writes);
}

static void test_corrupted_file_is_kept_and_replaced_by_defaults()
{
	FakeFileSystem fs;
	fs.files[ConfigStore::kPath] = "{\"schema\":1,";
	fs.files[ConfigStore::kCorruptedPath] = "an older corrupted file";
	ConfigStore store(fs);
	ConfigData out = defaults();

	TEST_ASSERT_EQUAL(static_cast<int>(Result::Replaced), static_cast<int>(store.load(defaultDevice(), out)));
	TEST_ASSERT_TRUE(out == defaults());
	TEST_ASSERT_EQUAL_STRING("{\"schema\":1,", fs.files[ConfigStore::kCorruptedPath].c_str());
	TEST_ASSERT_EQUAL_STRING(defaultsJson().c_str(), fs.files[ConfigStore::kPath].c_str());
}

static void test_invalid_values_and_oversized_files_count_as_corrupted()
{
	FakeFileSystem fs;
	fs.files[ConfigStore::kPath] = "{\"schema\":1,\"device\":{\"hostname\":\"-x\",\"log_level\":\"info\"},\"wifi\":{\"ssid\":\"\",\"password\":\"\"}}";
	ConfigStore store(fs);
	ConfigData out = defaults();

	TEST_ASSERT_EQUAL(static_cast<int>(Result::Replaced), static_cast<int>(store.load(defaultDevice(), out)));

	fs.files[ConfigStore::kPath] = std::string(ConfigStore::kMaxBytes + 1, ' ');
	TEST_ASSERT_EQUAL(static_cast<int>(Result::Replaced), static_cast<int>(store.load(defaultDevice(), out)));
}

static void test_lone_valid_temp_file_is_recovered()
{
	FakeFileSystem fs;
	// power lost between remove and rename
	fs.files[ConfigStore::kTempPath] = homeJson();
	ConfigStore store(fs);
	ConfigData out = defaults();

	TEST_ASSERT_EQUAL(static_cast<int>(Result::Recovered), static_cast<int>(store.load(defaultDevice(), out)));
	TEST_ASSERT_TRUE(out == homeConfig());
	TEST_ASSERT_EQUAL_STRING(homeJson().c_str(), fs.files[ConfigStore::kPath].c_str());
	TEST_ASSERT_FALSE(fs.exists(ConfigStore::kTempPath));
}

static void test_lone_partial_temp_file_is_discarded()
{
	FakeFileSystem fs;
	// power lost during the very first save
	fs.files[ConfigStore::kTempPath] = "{\"schema\":1,\"dev";
	ConfigStore store(fs);
	ConfigData out = defaults();

	TEST_ASSERT_EQUAL(static_cast<int>(Result::Created), static_cast<int>(store.load(defaultDevice(), out)));
	TEST_ASSERT_FALSE(fs.exists(ConfigStore::kTempPath));
	TEST_ASSERT_TRUE(out == defaults());
}

static void test_temp_file_beside_the_config_is_discarded()
{
	FakeFileSystem fs;
	fs.files[ConfigStore::kPath] = homeJson();
	// power lost before the swap
	fs.files[ConfigStore::kTempPath] = defaultsJson();
	ConfigStore store(fs);
	ConfigData out = defaults();

	TEST_ASSERT_EQUAL(static_cast<int>(Result::Loaded), static_cast<int>(store.load(defaultDevice(), out)));
	TEST_ASSERT_TRUE(out == homeConfig());
	TEST_ASSERT_FALSE(fs.exists(ConfigStore::kTempPath));
}

static void test_save_replaces_the_file_and_leaves_no_temp()
{
	FakeFileSystem fs;
	fs.files[ConfigStore::kPath] = defaultsJson();
	ConfigStore store(fs);

	TEST_ASSERT_TRUE(store.save(homeConfig()));
	TEST_ASSERT_EQUAL_STRING(homeJson().c_str(), fs.files[ConfigStore::kPath].c_str());
	TEST_ASSERT_FALSE(fs.exists(ConfigStore::kTempPath));
}

static void test_save_falls_back_when_rename_refuses_an_existing_file()
{
	FakeFileSystem fs;
	fs.renameReplaces = false;
	fs.files[ConfigStore::kPath] = defaultsJson();
	ConfigStore store(fs);

	TEST_ASSERT_TRUE(store.save(homeConfig()));
	TEST_ASSERT_EQUAL_STRING(homeJson().c_str(), fs.files[ConfigStore::kPath].c_str());
	TEST_ASSERT_FALSE(fs.exists(ConfigStore::kTempPath));
}

static void test_save_refuses_an_invalid_config()
{
	FakeFileSystem fs;
	fs.files[ConfigStore::kPath] = homeJson();
	ConfigStore store(fs);
	const ConfigData bad(DeviceConfig("relay", LogLevel::Info, utc(), false, DeviceConfig::kDefaultHeartbeatS,
		DeviceConfig::kUncalibratedBatteryScalePpm), WifiConfig("Home", "short", std::nullopt), {});

	TEST_ASSERT_FALSE(store.save(bad));
	TEST_ASSERT_EQUAL_INT(0, fs.writes);
	TEST_ASSERT_EQUAL_STRING(homeJson().c_str(), fs.files[ConfigStore::kPath].c_str());
}

static void test_save_refuses_a_config_above_the_size_limit()
{
	FakeFileSystem fs;
	fs.files[ConfigStore::kPath] = homeJson();
	ConfigStore store(fs);
	ConfigData big = homeConfig();
	std::string error;

	while (JsonText::write(big).size() <= ConfigStore::kMaxBytes)
	{
		big.addRule(longRule());
	}

	TEST_ASSERT_TRUE_MESSAGE(big.validate(error), error.c_str());
	TEST_ASSERT_FALSE(store.save(big));
	TEST_ASSERT_EQUAL_INT(0, fs.writes);
	TEST_ASSERT_EQUAL_STRING(homeJson().c_str(), fs.files[ConfigStore::kPath].c_str());
}

static void test_failed_or_corrupted_write_keeps_the_old_file()
{
	FakeFileSystem fs;
	fs.files[ConfigStore::kPath] = homeJson();
	ConfigStore store(fs);

	fs.writeWorks = false;
	TEST_ASSERT_FALSE(store.save(defaults()));
	TEST_ASSERT_EQUAL_STRING(homeJson().c_str(), fs.files[ConfigStore::kPath].c_str());
	TEST_ASSERT_FALSE(fs.exists(ConfigStore::kTempPath));

	fs.writeWorks = true;
	fs.writeTruncates = true;
	TEST_ASSERT_FALSE(store.save(defaults()));
	TEST_ASSERT_EQUAL_STRING(homeJson().c_str(), fs.files[ConfigStore::kPath].c_str());
	TEST_ASSERT_FALSE(fs.exists(ConfigStore::kTempPath));
}

static void test_failed_swap_keeps_the_temp_file_for_the_next_boot()
{
	FakeFileSystem fs;
	fs.files[ConfigStore::kPath] = defaultsJson();
	fs.renameWorks = false;
	ConfigStore store(fs);

	// the fallback removes the old file, then the rename fails
	TEST_ASSERT_FALSE(store.save(homeConfig()));
	TEST_ASSERT_FALSE(fs.exists(ConfigStore::kPath));
	TEST_ASSERT_EQUAL_STRING(homeJson().c_str(), fs.files[ConfigStore::kTempPath].c_str());

	fs.renameWorks = true;
	ConfigData out = defaults();
	TEST_ASSERT_EQUAL(static_cast<int>(Result::Recovered), static_cast<int>(store.load(defaultDevice(), out)));
	TEST_ASSERT_TRUE(out == homeConfig());
}

static void test_partial_file_takes_missing_keys_from_the_defaults()
{
	FakeFileSystem fs;
	fs.files[ConfigStore::kPath] = "{\"schema\":1,\"wifi\":{\"ssid\":\"Home\",\"password\":\"secret123\"}}";
	ConfigStore store(fs);
	const DeviceConfig start("fromdefaults", LogLevel::Warn, utc(), false, DeviceConfig::kDefaultHeartbeatS,
		DeviceConfig::kUncalibratedBatteryScalePpm);
	ConfigData out = defaults();

	TEST_ASSERT_EQUAL(static_cast<int>(Result::Loaded), static_cast<int>(store.load(start, out)));
	TEST_ASSERT_EQUAL_STRING("fromdefaults", out.device().hostname().c_str());
	TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::Warn), static_cast<int>(out.device().logLevel()));
	TEST_ASSERT_EQUAL_STRING("Home", out.wifi().ssid().c_str());
	// a valid file is never rewritten on load
	TEST_ASSERT_EQUAL_INT(0, fs.writes);
}

static void test_outcome_is_logged_before_the_defaults_are_saved()
{
	FakeFileSystem fs;
	ConfigStore store(fs);
	ConfigData out = defaults();

	store.load(defaultDevice(), out);

	char text[4096];
	size_t n = logRing.copyTo(text, sizeof(text) - 1);
	text[n] = '\0';

	const char* outcome = std::strstr(text, "not found, writing the defaults");
	const char* saved = std::strstr(text, "saved /config.json");

	TEST_ASSERT_NOT_NULL(outcome);
	TEST_ASSERT_NOT_NULL(saved);
	TEST_ASSERT_TRUE(outcome < saved);
}

static void test_load_logs_the_password_length_only()
{
	FakeFileSystem fs;
	fs.files[ConfigStore::kPath] = homeJson();
	ConfigStore store(fs);
	ConfigData out = defaults();

	store.load(defaultDevice(), out);

	char text[4096];
	size_t n = logRing.copyTo(text, sizeof(text) - 1);
	text[n] = '\0';

	TEST_ASSERT_NOT_NULL(std::strstr(text, "password set, 9 characters"));
	TEST_ASSERT_NULL(std::strstr(text, "secret123"));
}

static void test_load_lists_the_rules()
{
	FakeFileSystem fs;
	fs.files[ConfigStore::kPath] = "{\"schema\":1,\"rules\":[{\"id\":\"inv1\",\"target\":{\"address\":\"12:34:56:78:9a:bc\"},\"service_uuid\":\"fee7\","
		"\"tx\":{\"mode\":\"uuid\",\"uuid\":\"FED4\"}}]}";
	ConfigStore store(fs);
	ConfigData out = defaults();

	TEST_ASSERT_EQUAL(static_cast<int>(Result::Loaded), static_cast<int>(store.load(defaultDevice(), out)));

	char text[4096];
	size_t n = logRing.copyTo(text, sizeof(text) - 1);
	text[n] = '\0';

	TEST_ASSERT_NOT_NULL(std::strstr(text, "1 rule(s)"));
	TEST_ASSERT_NOT_NULL(std::strstr(text, "rule inv1 \"\": port 8899 to 12:34:56:78:9a:bc, service fee7, tx fed4, rx none"));
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_missing_file_writes_the_defaults);
	RUN_TEST(test_valid_file_loads_without_writing);
	RUN_TEST(test_corrupted_file_is_kept_and_replaced_by_defaults);
	RUN_TEST(test_invalid_values_and_oversized_files_count_as_corrupted);
	RUN_TEST(test_lone_valid_temp_file_is_recovered);
	RUN_TEST(test_lone_partial_temp_file_is_discarded);
	RUN_TEST(test_temp_file_beside_the_config_is_discarded);
	RUN_TEST(test_save_replaces_the_file_and_leaves_no_temp);
	RUN_TEST(test_save_falls_back_when_rename_refuses_an_existing_file);
	RUN_TEST(test_save_refuses_an_invalid_config);
	RUN_TEST(test_save_refuses_a_config_above_the_size_limit);
	RUN_TEST(test_failed_or_corrupted_write_keeps_the_old_file);
	RUN_TEST(test_failed_swap_keeps_the_temp_file_for_the_next_boot);
	RUN_TEST(test_partial_file_takes_missing_keys_from_the_defaults);
	RUN_TEST(test_outcome_is_logged_before_the_defaults_are_saved);
	RUN_TEST(test_load_logs_the_password_length_only);
	RUN_TEST(test_load_lists_the_rules);

	return UNITY_END();
}
