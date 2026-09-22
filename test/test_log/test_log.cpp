#include <unity.h>

#include <cstring>
#include <ctime>
#include <string>

#include "IdfLogLine.h"
#include "Lock.h"
#include "LogRing.h"
#include "LogStamp.h"
#include "Logger.h"

using w2b::Logger;
using w2b::LogLevel;
using w2b::LogRing;

namespace
{

uint32_t g_now = 0;
char g_sink[1024];
size_t g_sinkLen = 0;

size_t fakeStamp(char* out, size_t cap)
{
	return w2b::formatUptimeStamp(g_now, out, cap);
}

void captureSink(const char* line, size_t len)
{
	if (g_sinkLen + len < sizeof(g_sink))
	{
		memcpy(g_sink + g_sinkLen, line, len);
		g_sinkLen += len;
		g_sink[g_sinkLen] = '\0';
	}
}

// Copies the ring into a NUL-terminated string.
void ringText(const LogRing& ring, char* out, size_t cap)
{
	size_t n = ring.copyTo(out, cap - 1);
	out[n] = '\0';
}

// Counts lock/unlock calls and the deepest nesting seen.
struct FakeLock : w2b::Lock
{
	int depth = 0;

	int maxDepth = 0;

	int locks = 0;

	void lock() override
	{
		++depth;
		++locks;

		if (depth > maxDepth)
		{
			maxDepth = depth;
		}
	}

	void unlock() override
	{
		--depth;
	}
};

}  // namespace

void setUp()
{
	g_now = 0;
	g_sinkLen = 0;
	g_sink[0] = '\0';
}

void tearDown()
{
}

static void test_ring_keeps_lines_in_order()
{
	char storage[16];
	LogRing ring(storage, sizeof(storage));

	ring.push("one\n", 4);
	ring.push("two\n", 4);
	ring.push("three\n", 6);

	char out[32];
	ringText(ring, out, sizeof(out));

	TEST_ASSERT_EQUAL_STRING("one\ntwo\nthree\n", out);
	TEST_ASSERT_EQUAL_UINT(14, ring.size());
	TEST_ASSERT_EQUAL_UINT32(0, ring.droppedLines());
}

static void test_ring_drops_whole_oldest_lines_when_full()
{
	char storage[16];
	LogRing ring(storage, sizeof(storage));

	ring.push("one\n", 4);
	ring.push("two\n", 4);
	ring.push("three\n", 6);
	ring.push("four\n", 5);  // 2 bytes free: "one\n" goes

	char out[32];
	ringText(ring, out, sizeof(out));

	TEST_ASSERT_EQUAL_STRING("two\nthree\nfour\n", out);
	TEST_ASSERT_EQUAL_UINT(15, ring.size());
	TEST_ASSERT_EQUAL_UINT32(1, ring.droppedLines());
}

static void test_ring_wraps_around_storage_end()
{
	char storage[8];
	LogRing ring(storage, sizeof(storage));

	ring.push("abc\n", 4);
	ring.push("def\n", 4);
	ring.push("gh\n", 3);  // drops "abc\n", the new bytes wrap to index 0

	char out[16];
	ringText(ring, out, sizeof(out));

	TEST_ASSERT_EQUAL_STRING("def\ngh\n", out);
	TEST_ASSERT_EQUAL_UINT32(1, ring.droppedLines());
}

static void test_ring_cuts_oversized_line()
{
	char storage[16];
	LogRing ring(storage, sizeof(storage));

	ring.push("one\n", 4);
	ring.push("0123456789abcdefghij\n", 21);

	char out[32];
	ringText(ring, out, sizeof(out));

	TEST_ASSERT_EQUAL_STRING("0123456789abcde\n", out);
	TEST_ASSERT_EQUAL_UINT(16, ring.size());
	TEST_ASSERT_EQUAL_UINT32(1, ring.droppedLines());
}

static void test_ring_clear()
{
	char storage[16];
	LogRing ring(storage, sizeof(storage));

	ring.push("one\n", 4);
	ring.clear();

	TEST_ASSERT_EQUAL_UINT(0, ring.size());
	TEST_ASSERT_EQUAL_UINT(16, ring.capacity());
}

// Copies the ring from offset on into a NUL-terminated string; returns the offset of the first byte.
static uint64_t ringSince(const LogRing& ring, uint64_t offset, char* out, size_t cap)
{
	uint64_t from = 0;
	size_t n = ring.copySince(offset, out, cap - 1, from);
	out[n] = '\0';

	return from;
}

static void test_ring_offsets_count_every_stored_byte()
{
	char storage[16];
	LogRing ring(storage, sizeof(storage));

	TEST_ASSERT_EQUAL_UINT64(0, ring.startOffset());
	TEST_ASSERT_EQUAL_UINT64(0, ring.endOffset());

	ring.push("one\n", 4);
	ring.push("two\n", 4);
	ring.push("three\n", 6);
	ring.push("four\n", 5);  // "one\n" goes

	TEST_ASSERT_EQUAL_UINT64(4, ring.startOffset());
	TEST_ASSERT_EQUAL_UINT64(19, ring.endOffset());

	ring.clear();

	TEST_ASSERT_EQUAL_UINT64(19, ring.startOffset());
	TEST_ASSERT_EQUAL_UINT64(19, ring.endOffset());
}

static void test_ring_copies_since_an_offset()
{
	char storage[16];
	LogRing ring(storage, sizeof(storage));
	char out[32];

	ring.push("one\n", 4);
	ring.push("two\n", 4);

	TEST_ASSERT_EQUAL_UINT64(4, ringSince(ring, 4, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("two\n", out);

	TEST_ASSERT_EQUAL_UINT64(8, ringSince(ring, 8, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("", out);

	ring.push("three\n", 6);
	ring.push("four\n", 5);  // wraps, "one\n" goes

	TEST_ASSERT_EQUAL_UINT64(8, ringSince(ring, 8, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("three\nfour\n", out);

	// cap limits the copy; the next read goes on from where it stopped
	TEST_ASSERT_EQUAL_UINT64(8, ringSince(ring, 8, out, 4));
	TEST_ASSERT_EQUAL_STRING("thr", out);
}

static void test_ring_copy_since_a_lost_or_future_offset_starts_at_the_oldest_byte()
{
	char storage[16];
	LogRing ring(storage, sizeof(storage));
	char out[32];

	ring.push("one\n", 4);
	ring.push("two\n", 4);
	ring.push("three\n", 6);
	ring.push("four\n", 5);

	// offset 0 was dropped with "one\n"
	TEST_ASSERT_EQUAL_UINT64(4, ringSince(ring, 0, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("two\nthree\nfour\n", out);

	// a reader from before a restart
	TEST_ASSERT_EQUAL_UINT64(4, ringSince(ring, 1000, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("two\nthree\nfour\n", out);
}

static void test_logger_copies_a_range_in_pieces_under_the_lock()
{
	char storage[256];
	LogRing ring(storage, sizeof(storage));
	FakeLock lock;
	Logger logger(ring, fakeStamp, captureSink, &lock);

	Logger::info("t", "one");
	const uint64_t next = ring.endOffset();
	Logger::info("t", "two");

	uint64_t from = 0;
	uint64_t end = 0;
	Logger::findRange(next, from, end);
	TEST_ASSERT_EQUAL_UINT64(next, from);
	TEST_ASSERT_EQUAL_UINT64(ring.endOffset(), end);

	// logged after findRange(): not in the range
	Logger::info("t", "three");

	const int locksBefore = lock.locks;
	int calls = 0;
	std::string text;
	char piece[5];
	uint64_t offset = from;
	size_t count = 0;

	do
	{
		count = Logger::copyRange(offset, end, piece, sizeof(piece));
		text.append(piece, count);
		++calls;
	} while (count > 0);

	TEST_ASSERT_EQUAL_STRING("[       0 ms] I t: two\n", text.c_str());
	TEST_ASSERT_EQUAL_UINT64(end, offset);
	TEST_ASSERT_EQUAL_INT(calls, lock.locks - locksBefore);
	TEST_ASSERT_EQUAL_INT(0, lock.depth);
	TEST_ASSERT_EQUAL_INT(1, lock.maxDepth);
}

static void test_logger_range_skips_the_lines_dropped_meanwhile()
{
	char storage[16];
	LogRing ring(storage, sizeof(storage));
	Logger logger(ring, nullptr, nullptr);
	ring.push("a1\n", 3);
	ring.push("b2\n", 3);
	ring.push("c3\n", 3);

	uint64_t from = 0;
	uint64_t end = 0;
	Logger::findRange(0, from, end);
	TEST_ASSERT_EQUAL_UINT64(0, from);
	TEST_ASSERT_EQUAL_UINT64(9, end);

	std::string text;
	char piece[3];
	uint64_t offset = from;
	size_t count = Logger::copyRange(offset, end, piece, sizeof(piece));
	text.append(piece, count);
	TEST_ASSERT_EQUAL_UINT64(3, offset);

	// drops "a1\n", then "b2\n"
	ring.push("d4444444\n", 9);
	ring.push("e5\n", 3);

	while ((count = Logger::copyRange(offset, end, piece, sizeof(piece))) > 0)
	{
		text.append(piece, count);
	}

	TEST_ASSERT_EQUAL_STRING("a1\nc3\n", text.c_str());
	TEST_ASSERT_EQUAL_UINT64(end, offset);

	// a cleared ring ends the range at once
	Logger::findRange(0, from, end);
	ring.clear();
	offset = from;
	TEST_ASSERT_EQUAL_UINT(0, Logger::copyRange(offset, end, piece, sizeof(piece)));
	TEST_ASSERT_EQUAL_UINT64(end, offset);
}

static void test_uptime_stamp()
{
	char out[32];

	TEST_ASSERT_EQUAL_UINT(13, w2b::formatUptimeStamp(12345, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("[   12345 ms]", out);

	// cut to the room, still terminated
	TEST_ASSERT_EQUAL_UINT(4, w2b::formatUptimeStamp(12345, out, 5));
	TEST_ASSERT_EQUAL_STRING("[   ", out);
}

static void test_wall_stamp_is_day_month_year_and_milliseconds()
{
	std::tm local{};
	local.tm_mday = 25;
	local.tm_mon = 11;
	local.tm_year = 2026 - 1900;
	local.tm_hour = 15;
	local.tm_min = 38;
	local.tm_sec = 42;

	char out[40];

	TEST_ASSERT_EQUAL_UINT(25, w2b::formatWallStamp(local, 789, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("[25/12/2026 15:38:42.789]", out);

	local.tm_mday = 1;
	local.tm_mon = 0;
	local.tm_hour = 0;
	local.tm_min = 0;
	local.tm_sec = 5;
	w2b::formatWallStamp(local, 7, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("[01/01/2026 00:00:05.007]", out);
}

static size_t wallStamp(char* out, size_t cap)
{
	std::tm local{};
	local.tm_mday = 25;
	local.tm_mon = 11;
	local.tm_year = 2026 - 1900;

	return w2b::formatWallStamp(local, 0, out, cap);
}

static void test_logger_puts_the_stamp_first()
{
	char storage[256];
	LogRing ring(storage, sizeof(storage));
	Logger logger(ring, wallStamp, captureSink);

	Logger::info("wifi", "up");

	TEST_ASSERT_EQUAL_STRING("[25/12/2026 00:00:00.000] I wifi: up\n", g_sink);
}

static void test_logger_formats_timestamp_level_tag()
{
	char storage[256];
	LogRing ring(storage, sizeof(storage));
	Logger logger(ring, fakeStamp, captureSink);

	g_now = 12345;
	Logger::info("wifi", "connected to %s, rssi %d", "home", -61);

	TEST_ASSERT_EQUAL_STRING("[   12345 ms] I wifi: connected to home, rssi -61\n", g_sink);

	char out[256];
	ringText(ring, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING(g_sink, out);
}

static void test_logger_filters_below_level()
{
	char storage[256];
	LogRing ring(storage, sizeof(storage));
	Logger logger(ring, fakeStamp, captureSink);

	Logger::debug("t", "hidden");
	TEST_ASSERT_EQUAL_UINT(0, g_sinkLen);
	TEST_ASSERT_EQUAL_UINT(0, ring.size());

	Logger::setLevel(LogLevel::Debug);
	Logger::debug("t", "shown");
	TEST_ASSERT_EQUAL_STRING("[       0 ms] D t: shown\n", g_sink);

	Logger::setLevel(LogLevel::Error);
	Logger::warn("t", "hidden too");
	Logger::error("t", "kept");
	TEST_ASSERT_EQUAL_STRING("[       0 ms] D t: shown\n[       0 ms] E t: kept\n", g_sink);
}

static void test_logger_cuts_long_message_and_keeps_newline()
{
	char storage[512];
	LogRing ring(storage, sizeof(storage));
	Logger logger(ring, fakeStamp, captureSink);

	char big[300];
	memset(big, 'x', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';

	Logger::info("t", "%s", big);

	TEST_ASSERT_EQUAL_UINT(Logger::kMaxLine + 1, g_sinkLen);
	TEST_ASSERT_EQUAL_CHAR('\n', g_sink[g_sinkLen - 1]);
	TEST_ASSERT_EQUAL_CHAR('x', g_sink[g_sinkLen - 2]);
}

static void test_logger_works_without_sink()
{
	char storage[64];
	LogRing ring(storage, sizeof(storage));
	Logger logger(ring, fakeStamp, nullptr);

	Logger::warn("t", "ring only");

	TEST_ASSERT_EQUAL_UINT(0, g_sinkLen);
	TEST_ASSERT_TRUE(ring.size() > 0);
}

static void test_logger_holds_lock_once_per_emitted_line()
{
	char storage[256];
	LogRing ring(storage, sizeof(storage));
	FakeLock lock;
	Logger logger(ring, fakeStamp, captureSink, &lock);

	Logger::info("t", "one");
	Logger::info("t", "two");
	Logger::debug("t", "filtered before the lock");

	TEST_ASSERT_EQUAL_INT(2, lock.locks);
	TEST_ASSERT_EQUAL_INT(1, lock.maxDepth);
	TEST_ASSERT_EQUAL_INT(0, lock.depth);
}

static void test_lock_guard_releases_on_scope_exit_and_accepts_null()
{
	FakeLock lock;

	{
		w2b::LockGuard guard(&lock);
		TEST_ASSERT_EQUAL_INT(1, lock.depth);
	}

	TEST_ASSERT_EQUAL_INT(0, lock.depth);

	{
		w2b::LockGuard none(nullptr);  // single-task use
	}

	TEST_ASSERT_EQUAL_INT(1, lock.locks);
}

// Runs splitIdfLogLine on a copy of line.
static void splitCopy(const char* line, char* buffer, size_t size, LogLevel& level, const char*& tag, const char*& message)
{
	std::strncpy(buffer, line, size - 1);
	buffer[size - 1] = '\0';
	w2b::splitIdfLogLine(buffer, level, tag, message);
}

static void test_idf_line_is_split_into_level_tag_message()
{
	char buffer[128];
	LogLevel level = LogLevel::Debug;
	const char* tag = nullptr;
	const char* message = nullptr;

	splitCopy("E (389) esp_littlefs: ./lfs.c:1383:error: Corrupted dir pair at {0x0, 0x1}\n\n", buffer, sizeof(buffer), level, tag, message);
	TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::Error), static_cast<int>(level));
	TEST_ASSERT_EQUAL_STRING("esp_littlefs", tag);
	TEST_ASSERT_EQUAL_STRING("./lfs.c:1383:error: Corrupted dir pair at {0x0, 0x1}", message);

	splitCopy("I (568) wifi:wifi driver task: 3ffc0f5c, prio:23\n", buffer, sizeof(buffer), level, tag, message);
	TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::Info), static_cast<int>(level));
	TEST_ASSERT_EQUAL_STRING("wifi", tag);
	TEST_ASSERT_EQUAL_STRING("wifi driver task: 3ffc0f5c, prio:23", message);

	splitCopy("W (411) arduino: Mounting LittleFS failed! Error: -1\r\n", buffer, sizeof(buffer), level, tag, message);
	TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::Warn), static_cast<int>(level));
	TEST_ASSERT_EQUAL_STRING("arduino", tag);
	TEST_ASSERT_EQUAL_STRING("Mounting LittleFS failed! Error: -1", message);

	splitCopy("V (1) t: verbose", buffer, sizeof(buffer), level, tag, message);
	TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::Debug), static_cast<int>(level));
}

static void test_other_lines_keep_their_text_under_tag_idf()
{
	char buffer[128];
	LogLevel level = LogLevel::Error;
	const char* tag = nullptr;
	const char* message = nullptr;

	splitCopy("plain text\n", buffer, sizeof(buffer), level, tag, message);
	TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::Info), static_cast<int>(level));
	TEST_ASSERT_EQUAL_STRING("idf", tag);
	TEST_ASSERT_EQUAL_STRING("plain text", message);

	splitCopy("X (1) t: unknown level letter", buffer, sizeof(buffer), level, tag, message);
	TEST_ASSERT_EQUAL_STRING("idf", tag);
	TEST_ASSERT_EQUAL_STRING("X (1) t: unknown level letter", message);

	splitCopy("E (abc) t: no number", buffer, sizeof(buffer), level, tag, message);
	TEST_ASSERT_EQUAL_STRING("idf", tag);

	splitCopy("E (12) no colon at all", buffer, sizeof(buffer), level, tag, message);
	TEST_ASSERT_EQUAL_STRING("idf", tag);
	TEST_ASSERT_EQUAL_STRING("E (12) no colon at all", message);

	splitCopy("\r\n", buffer, sizeof(buffer), level, tag, message);
	TEST_ASSERT_EQUAL_STRING("", message);
}

static void test_level_names()
{
	TEST_ASSERT_EQUAL_STRING("error", w2b::logLevelName(LogLevel::Error));
	TEST_ASSERT_EQUAL_STRING("warn", w2b::logLevelName(LogLevel::Warn));
	TEST_ASSERT_EQUAL_STRING("info", w2b::logLevelName(LogLevel::Info));
	TEST_ASSERT_EQUAL_STRING("debug", w2b::logLevelName(LogLevel::Debug));
}

static void test_parse_level_names_round_trip_and_rejects_others()
{
	for (LogLevel level : w2b::LogLevels)
	{
		LogLevel parsed = LogLevel::Error;
		TEST_ASSERT_TRUE(w2b::parseLogLevel(w2b::logLevelName(level), parsed));
		TEST_ASSERT_EQUAL(static_cast<int>(level), static_cast<int>(parsed));
	}

	LogLevel untouched = LogLevel::Warn;
	TEST_ASSERT_FALSE(w2b::parseLogLevel("INFO", untouched));
	TEST_ASSERT_FALSE(w2b::parseLogLevel("", untouched));
	TEST_ASSERT_FALSE(w2b::parseLogLevel(nullptr, untouched));
	TEST_ASSERT_EQUAL(static_cast<int>(LogLevel::Warn), static_cast<int>(untouched));
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_ring_keeps_lines_in_order);
	RUN_TEST(test_ring_drops_whole_oldest_lines_when_full);
	RUN_TEST(test_ring_wraps_around_storage_end);
	RUN_TEST(test_ring_cuts_oversized_line);
	RUN_TEST(test_ring_clear);
	RUN_TEST(test_ring_offsets_count_every_stored_byte);
	RUN_TEST(test_ring_copies_since_an_offset);
	RUN_TEST(test_ring_copy_since_a_lost_or_future_offset_starts_at_the_oldest_byte);
	RUN_TEST(test_logger_copies_a_range_in_pieces_under_the_lock);
	RUN_TEST(test_logger_range_skips_the_lines_dropped_meanwhile);
	RUN_TEST(test_uptime_stamp);
	RUN_TEST(test_wall_stamp_is_day_month_year_and_milliseconds);
	RUN_TEST(test_logger_puts_the_stamp_first);
	RUN_TEST(test_logger_formats_timestamp_level_tag);
	RUN_TEST(test_logger_filters_below_level);
	RUN_TEST(test_logger_cuts_long_message_and_keeps_newline);
	RUN_TEST(test_logger_works_without_sink);
	RUN_TEST(test_logger_holds_lock_once_per_emitted_line);
	RUN_TEST(test_lock_guard_releases_on_scope_exit_and_accepts_null);
	RUN_TEST(test_idf_line_is_split_into_level_tag_message);
	RUN_TEST(test_other_lines_keep_their_text_under_tag_idf);
	RUN_TEST(test_level_names);
	RUN_TEST(test_parse_level_names_round_trip_and_rejects_others);

	return UNITY_END();
}
