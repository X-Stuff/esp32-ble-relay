#pragma once

#include <cstddef>
#include <cstdint>

#include "FixedString.h"

namespace w2b
{

// A GATT UUID as bytes in written order, in its written form: 16-bit "fee7", 32-bit "0000fee7" or 128-bit
// "0000fee7-0000-1000-8000-00805f9b34fb"; empty is none
class BleUuid
{
public:
	// the text form; a 128-bit UUID with its dashes is the longest
	using Text = FixedString<36>;

	// none
	BleUuid() = default;

	// the size bytes at bytes, in written order; a size other than 2, 4 or 16 is none
	BleUuid(const uint8_t* bytes, size_t size);

	// 2, 4 or 16; 0 for none
	inline size_t size() const { return length; }

	// none
	inline bool empty() const { return length == 0; }

	// size() bytes in written order
	inline const uint8_t* bytes() const { return value; }

	// Reads one of the three forms, either case, or "" for none; false for anything else, and uuid stays as it was.
	static bool parse(const char* text, BleUuid& uuid);

	// lower case, in the form it was read; "" for none
	Text text() const;

	// Converts to the 128-bit form: a 16- or 32-bit UUID on the Bluetooth base, 0000xxxx-0000-1000-8000-00805f9b34fb. An empty
	// UUID stays empty.
	BleUuid to128Bit() const;

	// true for the same UUID in any form: fed4, 0000fed4 and 0000fed4-0000-1000-8000-00805f9b34fb are equal
	inline bool equals(const BleUuid& other) const { return to128Bit() == other.to128Bit(); }

	// same form and bytes: fed4 and 0000fed4 differ
	bool operator==(const BleUuid& other) const;

	// not the same form and bytes
	inline bool operator!=(const BleUuid& other) const { return !(*this == other); }

private:
	uint8_t value[16] = {};

	uint8_t length = 0;
};

}  // namespace w2b
