#include "LogRing.h"

namespace w2b
{

LogRing::LogRing(char* storage, size_t capacity)
	: storage(storage), storageSize(capacity)
{
}

void LogRing::push(const char* data, size_t len)
{
	if (storageSize == 0 || len == 0)
	{
		return;
	}

	bool truncated = len > storageSize;

	if (truncated)
	{
		len = storageSize;
	}

	while (storageSize - used < len)
	{
		dropOldestLine();
	}

	for (size_t i = 0; i < len; ++i)
	{
		writeByte(used + i, data[i]);
	}

	used += len;
	pushed += len;

	if (truncated)
	{
		writeByte(used - 1, '\n');
	}
}

size_t LogRing::copyTo(char* dst, size_t cap) const
{
	size_t n = used < cap ? used : cap;

	for (size_t i = 0; i < n; ++i)
	{
		dst[i] = readByte(i);
	}

	return n;
}

size_t LogRing::copySince(uint64_t offset, char* dst, size_t cap, uint64_t& from) const
{
	from = offset < startOffset() || offset > pushed ? startOffset() : offset;

	const size_t skip = static_cast<size_t>(from - startOffset());
	const size_t available = used - skip;
	const size_t n = available < cap ? available : cap;

	for (size_t i = 0; i < n; ++i)
	{
		dst[i] = readByte(skip + i);
	}

	return n;
}

void LogRing::clear()
{
	head = 0;
	used = 0;
}

// Removes bytes from the oldest end up to and including the first newline.
void LogRing::dropOldestLine()
{
	while (used > 0)
	{
		char c = readByte(0);
		head = (head + 1) % storageSize;
		--used;

		if (c == '\n')
		{
			break;
		}
	}

	++dropped;
}

// index counts from the oldest stored byte.
void LogRing::writeByte(size_t index, char c)
{
	storage[(head + index) % storageSize] = c;
}

char LogRing::readByte(size_t index) const
{
	return storage[(head + index) % storageSize];
}

}  // namespace w2b
