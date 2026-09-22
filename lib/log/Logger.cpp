#include "Logger.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace w2b
{

namespace
{

char levelChar(LogLevel level)
{
	switch (level)
	{
		case LogLevel::Error: return 'E';
		case LogLevel::Warn:  return 'W';
		case LogLevel::Info:  return 'I';
		case LogLevel::Debug: return 'D';
	}

	return '?';
}

// Turns an snprintf result into the number of characters actually stored.
size_t stored(int written, size_t room)
{
	if (written < 0)
	{
		return 0;
	}

	size_t n = static_cast<size_t>(written);

	return n < room ? n : room;
}

}  // namespace

const char* logLevelName(LogLevel level)
{
	switch (level)
	{
		case LogLevel::Error: return "error";
		case LogLevel::Warn:  return "warn";
		case LogLevel::Info:  return "info";
		case LogLevel::Debug: return "debug";
	}

	return "?";
}

bool parseLogLevel(const char* name, LogLevel& level)
{
	for (LogLevel candidate : LogLevels)
	{
		if (name != nullptr && std::strcmp(name, logLevelName(candidate)) == 0)
		{
			level = candidate;
			return true;
		}
	}

	return false;
}

Logger* Logger::instance = nullptr;

Logger::Logger(LogRing& ring, Stamp stamp, Sink sink, Lock* lock)
	: ring(ring), stamp(stamp), sink(sink), mutex(lock)
{
	assert(instance == nullptr);
	instance = this;
}

Logger::~Logger()
{
	if (instance == this)
	{
		instance = nullptr;
	}
}

void Logger::setLevel(LogLevel level)
{
	instance->threshold = level;
}

bool Logger::enabled(LogLevel level)
{
	return static_cast<uint8_t>(level) <= static_cast<uint8_t>(instance->threshold);
}

void Logger::vlog(LogLevel level, const char* tag, const char* fmt, va_list args)
{
	instance->internalLog(level, tag, fmt, args);
}

void Logger::findRange(uint64_t offset, uint64_t& from, uint64_t& end)
{
	instance->internalFindRange(offset, from, end);
}

size_t Logger::copyRange(uint64_t& offset, uint64_t end, char* out, size_t room)
{
	return instance->internalCopyRange(offset, end, out, room);
}

void Logger::internalLog(LogLevel level, const char* tag, const char* fmt, va_list args)
{
	if (!enabled(level))
	{
		return;
	}

	LockGuard guard(mutex);

	// kMaxLine characters, then the newline, then the terminator.
	char line[kMaxLine + 2];

	line[0] = '\0';
	size_t len = stamp != nullptr ? stamp(line, kMaxLine + 1) : 0;

	int n = snprintf(line + len, kMaxLine + 1 - len, " %c %s: ", levelChar(level), tag);
	len += stored(n, kMaxLine - len);

	n = vsnprintf(line + len, kMaxLine + 1 - len, fmt, args);
	len += stored(n, kMaxLine - len);

	line[len++] = '\n';
	line[len] = '\0';

	ring.push(line, len);

	if (sink != nullptr)
	{
		sink(line, len);
	}
}

// A copySince of no bytes only resolves where a copy from offset starts.
void Logger::internalFindRange(uint64_t offset, uint64_t& from, uint64_t& end) const
{
	LockGuard guard(mutex);

	ring.copySince(offset, nullptr, 0, from);
	end = ring.endOffset();
}

size_t Logger::internalCopyRange(uint64_t& offset, uint64_t end, char* out, size_t room) const
{
	LockGuard guard(mutex);

	uint64_t from = 0;
	ring.copySince(offset, nullptr, 0, from);

	// the ring was cleared, or dropped every line before end
	if (from >= end)
	{
		offset = end;
		return 0;
	}

	const size_t count = ring.copySince(from, out, static_cast<size_t>(std::min<uint64_t>(end - from, room)), from);
	offset = from + count;

	return count;
}

void Logger::log(LogLevel level, const char* tag, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vlog(level, tag, fmt, args);
	va_end(args);
}

void Logger::error(const char* tag, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vlog(LogLevel::Error, tag, fmt, args);
	va_end(args);
}

void Logger::warn(const char* tag, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vlog(LogLevel::Warn, tag, fmt, args);
	va_end(args);
}

void Logger::info(const char* tag, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vlog(LogLevel::Info, tag, fmt, args);
	va_end(args);
}

void Logger::debug(const char* tag, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vlog(LogLevel::Debug, tag, fmt, args);
	va_end(args);
}

}  // namespace w2b
