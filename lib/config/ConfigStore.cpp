#include "ConfigStore.h"

#include "ConfigRules.h"
#include "IPv4Str.h"
#include "JsonText.h"
#include "Logger.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "config";

const char* resultName(ConfigStore::LoadResult result)
{
	switch (result)
	{
		case ConfigStore::LoadResult::Loaded:    return "loaded";
		case ConfigStore::LoadResult::Recovered: return "recovered from the temp file of an interrupted save";
		case ConfigStore::LoadResult::Created:   return "not found, writing the defaults";
		case ConfigStore::LoadResult::Replaced:  return "corrupted, writing the defaults";
	}

	return "?";
}

BleUuid::Text channelText(ChannelMode mode, const BleUuid& uuid)
{
	switch (mode)
	{
		case ChannelMode::Uuid: return uuid.text();
		case ChannelMode::None: return BleUuid::Text("none");
	}

	return BleUuid::Text("?");
}

// the address, or the name the device is found by
std::string targetText(const TargetConfig& target)
{
	return target.address() ? std::string(target.address()->text().c_str()) : "the device named \"" + std::string(target.name().c_str()) + "\"";
}

}  // namespace

ConfigStore::ConfigStore(FileSystem& fs)
	: fs(fs)
{
}

ConfigStore::LoadResult ConfigStore::load(const DeviceConfig& deviceDefaults, ConfigData& out)
{
	const LoadResult result = loadFile(deviceDefaults, out);

	logSummary(out);

	return result;
}

bool ConfigStore::save(const ConfigData& config)
{
	std::string error;

	if (!config.validate(error))
	{
		Logger::error(kTag, "not saved, invalid: %s", error.c_str());
		return false;
	}

	const std::string json = JsonText::write(config);

	if (json.size() > kMaxBytes)
	{
		Logger::error(kTag, "not saved: %u bytes, the file holds at most %u", (unsigned)json.size(), (unsigned)kMaxBytes);
		return false;
	}

	if (!writeVerified(json))
	{
		fs.remove(kTempPath);
		return false;
	}

	// On failure the temp file stays: with the old file already gone it is the only copy, and load() recovers it.
	if (!swapIn())
	{
		return false;
	}

	Logger::info(kTag, "saved %s, %u bytes", kPath, (unsigned)json.size());

	return true;
}

ConfigStore::LoadResult ConfigStore::loadFile(const DeviceConfig& deviceDefaults, ConfigData& out)
{
	if (recoverTemp(deviceDefaults, out))
	{
		return report(LoadResult::Recovered);
	}

	if (!fs.exists(kPath))
	{
		return useDefaults(report(LoadResult::Created), deviceDefaults, out);
	}

	std::string error;

	if (readValid(kPath, deviceDefaults, out, error))
	{
		return report(LoadResult::Loaded);
	}

	quarantine(error);

	return useDefaults(report(LoadResult::Replaced), deviceDefaults, out);
}

ConfigStore::LoadResult ConfigStore::report(LoadResult result) const
{
	Logger::info(kTag, "%s %s", kPath, resultName(result));

	return result;
}

// Beside the config file a temp file is the leftover of a save that never swapped: discarded. Alone it is the only copy: kept if valid.
bool ConfigStore::recoverTemp(const DeviceConfig& deviceDefaults, ConfigData& out)
{
	if (!fs.exists(kTempPath))
	{
		return false;
	}

	std::string error;

	if (fs.exists(kPath) || !readValid(kTempPath, deviceDefaults, out, error))
	{
		Logger::warn(kTag, "discarding %s left by an interrupted save%s%s", kTempPath, error.empty() ? "" : ": ", error.c_str());
		fs.remove(kTempPath);
		return false;
	}

	if (!fs.rename(kTempPath, kPath))
	{
		Logger::error(kTag, "renaming %s to %s failed; running on its content, the next boot tries again", kTempPath, kPath);
	}

	return true;
}

bool ConfigStore::readValid(const char* path, const DeviceConfig& deviceDefaults, ConfigData& out, std::string& error) const
{
	std::string json;

	if (!fs.read(path, kMaxBytes, json))
	{
		error = "unreadable or larger than " + std::to_string(kMaxBytes) + " bytes";
		return false;
	}

	out.reset(deviceDefaults);

	return JsonText::readInto(json, out, error);
}

void ConfigStore::quarantine(const std::string& reason)
{
	Logger::error(kTag, "%s is corrupted: %s; kept as %s", kPath, reason.c_str(), kCorruptedPath);

	if (fs.exists(kCorruptedPath))
	{
		fs.remove(kCorruptedPath);
	}

	if (!fs.rename(kPath, kCorruptedPath))
	{
		fs.remove(kPath);
	}
}

// The defaults run from RAM even when writing them fails; save() logs that.
ConfigStore::LoadResult ConfigStore::useDefaults(LoadResult result, const DeviceConfig& deviceDefaults, ConfigData& out)
{
	out.reset(deviceDefaults);
	save(out);

	return result;
}

// Reading the temp file back keeps a silent write error from replacing a good config.
bool ConfigStore::writeVerified(const std::string& json)
{
	std::string stored;

	if (fs.write(kTempPath, json) && fs.read(kTempPath, kMaxBytes, stored) && stored == json)
	{
		return true;
	}

	Logger::error(kTag, "not saved: writing %s failed", kTempPath);

	return false;
}

bool ConfigStore::swapIn()
{
	if (fs.rename(kTempPath, kPath))
	{
		return true;
	}

	// The temp file is complete, so removing the old file first is safe: a power loss in between leaves the temp file for load().
	if (fs.exists(kPath) && fs.remove(kPath) && fs.rename(kTempPath, kPath))
	{
		return true;
	}

	Logger::error(kTag, "not saved: replacing %s with %s failed", kPath, kTempPath);

	return false;
}

void ConfigStore::logSummary(const ConfigData& config) const
{
	const DeviceConfig& device = config.device();

	Logger::info(kTag, "device hostname %s, log level %s, time zone %s, %s BLE address", device.hostname().c_str(),
		logLevelName(device.logLevel()), device.timeZone().name(), device.useRandomBleAddress() ? "random" : "public");

	logWifi(config.wifi());
	logRules(config.rules());
}

void ConfigStore::logWifi(const WifiConfig& wifi) const
{
	if (wifi.ssid().empty())
	{
		Logger::info(kTag, "wifi not configured");
		return;
	}

	Logger::info(kTag, "wifi ssid \"%s\", password %s, %u characters", wifi.ssid().c_str(), wifi.password().empty() ? "none" : "set",
		(unsigned)wifi.password().size());

	if (!wifi.staticIp())
	{
		Logger::info(kTag, "wifi address by DHCP");
		return;
	}

	const StaticIpConfig& address = *wifi.staticIp();

	Logger::info(kTag, "wifi static address %s, subnet %s, gateway %s, dns %s", IPv4Str(address.ip()).c_str(), IPv4Str(address.subnet()).c_str(),
		IPv4Str(address.gateway()).c_str(), address.dns() != 0 ? IPv4Str(address.dns()).c_str() : "none");
}

void ConfigStore::logRules(const ConfigData::Rules& rules) const
{
	Logger::info(kTag, "%u rule(s)", (unsigned)rules.size());

	for (const RuleConfig& rule : rules)
	{
		Logger::info(kTag, "rule %s \"%s\"%s: port %u to %s, service %s, tx %s, rx %s", rule.id().c_str(), rule.name().c_str(),
			rule.enabled() ? "" : " (disabled)", (unsigned)rule.listenPort(), targetText(rule.target()).c_str(),
			rule.serviceUuid().text().c_str(), channelText(rule.tx().mode(), rule.tx().uuid()).c_str(),
			channelText(rule.rx().mode(), rule.rx().uuid()).c_str());
	}
}

}  // namespace w2b
