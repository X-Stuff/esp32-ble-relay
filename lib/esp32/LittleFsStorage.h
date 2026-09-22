#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "FileSystem.h"

namespace w2b
{

// FileSystem over the Arduino LittleFS library on the data partition. Firmware only.
class LittleFsStorage : public FileSystem
{
public:
	// what mount() did
	enum class MountResult : uint8_t
	{
		// mounted the LittleFS the partition held
		Mounted,

		// there was no LittleFS; the partition was formatted, then mounted
		Formatted,

		// not mounted, not even after formatting
		Failed,
	};

	// partitionLabel: the data partition's label in the partition table; the pointer is kept
	explicit LittleFsStorage(const char* partitionLabel);

	// Mounts the partition; when it holds no LittleFS (first boot), formats it first.
	// State-changing: formatting erases the data partition only, never the firmware.
	MountResult mount();

	// size of the file system in bytes
	size_t totalBytes() const;

	// bytes in use
	size_t usedBytes() const;

	// true when path exists; paths start with "/"
	bool exists(const char* path) const override;

	// false when missing, unreadable, larger than maxBytes or read short; out is left unchanged then
	bool read(const char* path, size_t maxBytes, std::string& out) const override;

	// creates or truncates; the file is committed before this returns
	bool write(const char* path, const std::string& data) override;

	// renames the file at from to to
	bool rename(const char* from, const char* to) override;

	// false when the file is missing or LittleFS cannot remove it
	bool remove(const char* path) override;

private:
	const char* partitionLabel;
};

}  // namespace w2b
