// Hex bytes in text, as BLE addresses and UUIDs write them.
#pragma once

#include <cstdint>

namespace w2b
{

// Reads the two hex digits at text, either case; false unless both are hex digits.
bool readHexByte(const char* text, uint8_t& byte);

// Writes byte as two lower case hex digits at out, without a terminator.
void writeHexByte(uint8_t byte, char* out);

}  // namespace w2b
