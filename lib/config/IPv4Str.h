#pragma once

#include <cstddef>
#include <cstdint>

namespace w2b
{

// An IPv4 address as a dotted quad, "192.168.0.1", from its host-order value, 0xc0a80001; built digit by digit, no printf.
class IPv4Str
{
public:
	// characters of the longest text, "255.255.255.255", without the terminator
	static constexpr size_t kMaxLength = 15;

	explicit IPv4Str(uint32_t address);

	// zero-terminated
	inline const char* c_str() const { return text; }

private:
	// the octet's decimal digits, no leading zeros, from position at; the position after them
	size_t writeOctet(size_t at, uint8_t octet);

	char text[kMaxLength + 1];
};

}  // namespace w2b
