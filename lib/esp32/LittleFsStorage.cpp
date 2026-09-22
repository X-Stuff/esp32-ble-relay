#include "LittleFsStorage.h"

#include <LittleFS.h>

namespace w2b
{

namespace
{

const char* const kBasePath = "/littlefs";
const uint8_t kMaxOpenFiles = 5;

}  // namespace

LittleFsStorage::LittleFsStorage(const char* partitionLabel)
	: partitionLabel(partitionLabel)
{
}

LittleFsStorage::MountResult LittleFsStorage::mount()
{
	if (LittleFS.begin(false, kBasePath, kMaxOpenFiles, partitionLabel))
	{
		return MountResult::Mounted;
	}

	// State-changing: the partition holds no LittleFS, so format it. The failed attempt above has logged an E line.
	if (LittleFS.begin(true, kBasePath, kMaxOpenFiles, partitionLabel))
	{
		return MountResult::Formatted;
	}

	return MountResult::Failed;
}

size_t LittleFsStorage::totalBytes() const
{
	return LittleFS.totalBytes();
}

size_t LittleFsStorage::usedBytes() const
{
	return LittleFS.usedBytes();
}

bool LittleFsStorage::exists(const char* path) const
{
	return LittleFS.exists(path);
}

bool LittleFsStorage::read(const char* path, size_t maxBytes, std::string& out) const
{
	if (!exists(path))
	{
		return false;
	}

	File file = LittleFS.open(path, FILE_READ);

	if (!file || file.size() > maxBytes)
	{
		return false;
	}

	std::string data(file.size(), '\0');
	size_t got = data.empty() ? 0 : file.read(reinterpret_cast<uint8_t*>(&data[0]), data.size());

	if (got != data.size())
	{
		return false;
	}

	out = std::move(data);

	return true;
}

// LittleFS commits the file when it is closed; the File destructor closes it.
bool LittleFsStorage::write(const char* path, const std::string& data)
{
	File file = LittleFS.open(path, FILE_WRITE);

	if (!file)
	{
		return false;
	}

	return file.write(reinterpret_cast<const uint8_t*>(data.data()), data.size()) == data.size();
}

bool LittleFsStorage::rename(const char* from, const char* to)
{
	return LittleFS.rename(from, to);
}

bool LittleFsStorage::remove(const char* path)
{
	return exists(path) && LittleFS.remove(path);
}

}  // namespace w2b
