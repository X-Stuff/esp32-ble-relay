#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

namespace w2b
{

// Log line stamp "[   12345 ms]", milliseconds since boot, for lines before the clock is set. Returns the characters written, at most
// cap - 1, NUL-terminated.
size_t formatUptimeStamp(uint32_t uptimeMs, char* out, size_t cap);

// Log line stamp "[25/12/2026 15:38:42.789]", local date and time. Returns the characters written, at most cap - 1, NUL-terminated.
size_t formatWallStamp(const std::tm& local, uint32_t millis, char* out, size_t cap);

}  // namespace w2b
