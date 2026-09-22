#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "ConfigData.h"
#include "FileSystem.h"

namespace w2b
{

// Keeps ConfigData as /config.json: a save goes to a temp file, is read back, then replaces the old file by rename; load() repairs what
// an interrupted save leaves and never fails. Application thread only. Writes /config.json, its temp file and its .corrupted copy.
class ConfigStore
{
public:
	// the configuration
	static constexpr const char* kPath = "/config.json";

	// a save before its rename
	static constexpr const char* kTempPath = "/config.json.tmp";

	// the last corrupted file, kept for inspection
	static constexpr const char* kCorruptedPath = "/config.json.corrupted";

	// The file's size limit, and so the number of rules: save() refuses a bigger config, load() counts a bigger file as corrupted.
	static constexpr size_t kMaxBytes = 28672;

	// What load() found
	enum class LoadResult : uint8_t
	{
		// a valid file
		Loaded,

		// only the temp file of an interrupted save, valid; it became the file
		Recovered,

		// no file; the defaults were written
		Created,

		// a corrupted file, kept as kCorruptedPath; the defaults were written
		Replaced,
	};

	// fs: holds the files
	explicit ConfigStore(FileSystem& fs);

	// Loads the file into out in place, keys it leaves out taking the defaults: deviceDefaults and those of the other sections. A missing
	// or corrupted file is replaced by those defaults. Logs the result, the WiFi password as its length only.
	LoadResult load(const DeviceConfig& deviceDefaults, ConfigData& out);

	// Refuses an invalid config and one above kMaxBytes. On failure the previous file stays, or its temp copy when the old one is gone.
	bool save(const ConfigData& config);

private:
	LoadResult loadFile(const DeviceConfig& deviceDefaults, ConfigData& out);

	LoadResult report(LoadResult result) const;

	bool recoverTemp(const DeviceConfig& deviceDefaults, ConfigData& out);

	bool readValid(const char* path, const DeviceConfig& deviceDefaults, ConfigData& out, std::string& error) const;

	void quarantine(const std::string& reason);

	LoadResult useDefaults(LoadResult result, const DeviceConfig& deviceDefaults, ConfigData& out);

	bool writeVerified(const std::string& json);

	bool swapIn();

	void logSummary(const ConfigData& config) const;

	void logWifi(const WifiConfig& wifi) const;

	void logRules(const ConfigData::Rules& rules) const;

	FileSystem& fs;
};

}  // namespace w2b
