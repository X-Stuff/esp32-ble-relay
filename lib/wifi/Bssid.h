#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace w2b
{

// an access point's MAC address
using BSSID = std::array<uint8_t, 6>;

// A BSSID as text, "12:34:56:78:9A:BC", built from a hex digit table: no printf.
class BSSIDStr
{
public:
	// characters of the text, without the terminator
	static constexpr size_t kLength = 17;

	explicit BSSIDStr(const BSSID& bssid);

	// zero-terminated, upper-case hex pairs
	inline const char* c_str() const { return text; }

private:
	char text[kLength + 1];
};

}  // namespace w2b
