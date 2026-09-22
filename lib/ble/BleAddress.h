#pragma once

#include <array>
#include <cstdint>

#include "FixedString.h"

namespace w2b
{

// A Bluetooth device address as six bytes in written order: "12:34:56:78:9a:bc" is {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc}
class BleAddress
{
public:
	// the text form, "12:34:56:78:9a:bc"
	using Text = FixedString<17>;

	// the six bytes in written order
	using Bytes = std::array<uint8_t, 6>;

	// 00:00:00:00:00:00
	BleAddress() = default;

	// bytes in written order
	explicit BleAddress(const Bytes& bytes)
		: value(bytes)
	{
	}

	// in written order
	inline const Bytes& bytes() const { return value; }

	// Reads six two-digit hex bytes separated by colons, either case; false for anything else, and address stays as it was.
	static bool parse(const char* text, BleAddress& address);

	// lower case, "12:34:56:78:9a:bc"
	Text text() const;

	// same six bytes
	inline bool operator==(const BleAddress& other) const { return value == other.value; }

	// not the same six bytes
	inline bool operator!=(const BleAddress& other) const { return value != other.value; }

private:
	Bytes value{};
};

}  // namespace w2b
