#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <array>

#include "Lock.h"
#include "LogRing.h"

namespace w2b
{

// The severity of a log line, most severe first
enum class LogLevel : uint8_t
{
	// a failure, "E" in the line
	Error = 0,

	// a problem that was handled, "W"
	Warn = 1,

	// a normal event, "I"
	Info = 2,

	// detail for debugging, "D"
	Debug = 3,
};

static constexpr std::array<LogLevel, 4> LogLevels = { LogLevel::Error, LogLevel::Warn, LogLevel::Info, LogLevel::Debug };

// "error", "warn", "info" or "debug"; "?" for a value outside the enum
const char* logLevelName(LogLevel level);

// The reverse of logLevelName: "error", "warn", "info" or "debug"; false for anything else, level untouched.
bool parseLogLevel(const char* name, LogLevel& level);

// Leveled logger: each line, "[25/12/2026 15:38:42.789] I wifi: connected\n" ("[   12345 ms]" before the clock is set), goes to a
// LogRing (served by /api/log) and to a sink (Serial on the board, a buffer in tests). Used through static methods.
class Logger
{
public:
	// longest line without the newline; longer messages are cut
	static constexpr size_t kMaxLine = 212;

	// Writes the line's time stamp into out, NUL-terminated; returns the characters written, at most cap - 1. Runs under the lock.
	using Stamp = size_t (*)(char* out, size_t cap);

	// receives every stored line: len bytes, newline included, NUL-terminated; runs under the lock
	using Sink = void (*)(const char* line, size_t len);

	// Becomes the instance the static methods use; asserts that there is no other. lock: held per line; null for single-task use.
	Logger(LogRing& ring, Stamp stamp, Sink sink, Lock* lock = nullptr);

	~Logger();

	Logger(const Logger&) = delete;

	Logger& operator=(const Logger&) = delete;

	// the most verbose level that still prints
	static inline LogLevel level() { return instance->threshold; }

	// sets the most verbose level that still prints
	static void setLevel(LogLevel level);

	// true when a line at level prints
	static bool enabled(LogLevel level);

	// formats one line at level, printf style, when the level is enabled
	static void log(LogLevel level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

	// log() with a va_list
	static void vlog(LogLevel level, const char* tag, const char* fmt, va_list args);

	// log() at LogLevel::Error
	static void error(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

	// log() at LogLevel::Warn
	static void warn(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

	// log() at LogLevel::Info
	static void info(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

	// log() at LogLevel::Debug
	static void debug(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

	// Where the ring's bytes from offset on start (LogRing::copySince), and the offset past the newest one, read together under the
	// lock: the range copyRange() copies.
	static void findRange(uint64_t offset, uint64_t& from, uint64_t& end);

	// Copies the ring's bytes from offset on, before end and at most room of them, into out under the lock, and moves offset past them;
	// returns how many, 0 once offset reaches end. Offset jumps to the oldest byte when the ring dropped the lines at it.
	static size_t copyRange(uint64_t& offset, uint64_t end, char* out, size_t room);

private:
	void internalLog(LogLevel level, const char* tag, const char* fmt, va_list args);

	void internalFindRange(uint64_t offset, uint64_t& from, uint64_t& end) const;

	size_t internalCopyRange(uint64_t& offset, uint64_t end, char* out, size_t room) const;

	static Logger* instance;

	LogRing& ring;

	Stamp stamp;

	Sink sink;

	Lock* mutex;

	// most verbose level that still prints
	LogLevel threshold = LogLevel::Info;
};

}  // namespace w2b
