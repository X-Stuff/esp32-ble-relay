#include "LogStamp.h"

#include <cstdio>

namespace w2b
{

namespace
{

// Turns an snprintf result into the number of characters actually stored.
size_t stored(int written, size_t cap)
{
	if (written < 0 || cap == 0)
	{
		return 0;
	}

	size_t n = static_cast<size_t>(written);

	return n < cap ? n : cap - 1;
}

}  // namespace

size_t formatUptimeStamp(uint32_t uptimeMs, char* out, size_t cap)
{
	return stored(snprintf(out, cap, "[%8lu ms]", static_cast<unsigned long>(uptimeMs)), cap);
}

size_t formatWallStamp(const std::tm& local, uint32_t millis, char* out, size_t cap)
{
	return stored(snprintf(out, cap, "[%02d/%02d/%04d %02d:%02d:%02d.%03u]", local.tm_mday, local.tm_mon + 1, local.tm_year + 1900,
		local.tm_hour, local.tm_min, local.tm_sec, static_cast<unsigned>(millis % 1000)), cap);
}

}  // namespace w2b
