#pragma once

#include <cstddef>
#include <string>

namespace w2b
{

// Whole-file storage by path. The firmware implements it over LittleFS (lib/esp32/LittleFsStorage); tests use an in-memory fake.
class FileSystem
{
public:
	virtual ~FileSystem() = default;

	// true when path exists
	virtual bool exists(const char* path) const = 0;

	// out gets the whole file; false when missing, unreadable or larger than maxBytes
	virtual bool read(const char* path, size_t maxBytes, std::string& out) const = 0;

	// creates or truncates path and writes data; true only when every byte went out
	virtual bool write(const char* path, const std::string& data) = 0;

	// moves from to to; may refuse to replace an existing destination
	virtual bool rename(const char* from, const char* to) = 0;

	// deletes path; true on success
	virtual bool remove(const char* path) = 0;
};

}  // namespace w2b
