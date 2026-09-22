#pragma once

#include <cstddef>
#include <cstdint>

namespace w2b
{

// Byte ring of complete log lines in caller-supplied storage, oldest first; whole lines are dropped from the oldest end to make room.
// Every byte has an offset counting all bytes stored before it (GET /api/log?since=N). Not thread-safe: Logger locks around it.
class LogRing
{
public:
	// storage: capacity bytes, owned by the caller
	LogRing(char* storage, size_t capacity);

	// bytes stored
	inline size_t size() const { return used; }

	// the storage size in bytes
	inline size_t capacity() const { return storageSize; }

	// lines removed to make room since construction
	inline uint32_t droppedLines() const { return dropped; }

	// offset of the oldest stored byte
	inline uint64_t startOffset() const { return pushed - used; }

	// offset just past the newest byte: all bytes ever stored
	inline uint64_t endOffset() const { return pushed; }

	// Appends one line, newline included. A line longer than the capacity is cut to the capacity and its last byte becomes the newline.
	void push(const char* data, size_t len);

	// copies up to cap bytes, oldest first, not NUL-terminated; returns the number copied
	size_t copyTo(char* dst, size_t cap) const;

	// Copies up to cap bytes from offset on, not NUL-terminated; returns the number copied, from gets the first one's offset. An offset
	// older than the oldest stored byte, or past the newest (a reader from before a restart), starts at the oldest.
	size_t copySince(uint64_t offset, char* dst, size_t cap, uint64_t& from) const;

	// removes every line; offsets keep counting
	void clear();

private:
	void dropOldestLine();

	void writeByte(size_t index, char c);

	char readByte(size_t index) const;

	char* storage;

	size_t storageSize;

	// index of the oldest byte
	size_t head = 0;

	// bytes stored
	size_t used = 0;

	// lines removed to make room
	uint32_t dropped = 0;

	// bytes stored since construction; dropped lines and clear() leave it as it is
	uint64_t pushed = 0;
};

}  // namespace w2b
