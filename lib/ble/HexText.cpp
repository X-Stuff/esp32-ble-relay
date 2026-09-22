#include "HexText.h"

namespace w2b
{

namespace
{

bool digitValue(char c, uint8_t& value)
{
	if (c >= '0' && c <= '9')
	{
		value = static_cast<uint8_t>(c - '0');
		return true;
	}

	if (c >= 'a' && c <= 'f')
	{
		value = static_cast<uint8_t>(c - 'a' + 10);
		return true;
	}

	if (c >= 'A' && c <= 'F')
	{
		value = static_cast<uint8_t>(c - 'A' + 10);
		return true;
	}

	return false;
}

}  // namespace

bool readHexByte(const char* text, uint8_t& byte)
{
	uint8_t high = 0;
	uint8_t low = 0;

	if (!digitValue(text[0], high) || !digitValue(text[1], low))
	{
		return false;
	}

	byte = static_cast<uint8_t>(high << 4 | low);

	return true;
}

void writeHexByte(uint8_t byte, char* out)
{
	constexpr const char* kDigits = "0123456789abcdef";

	out[0] = kDigits[byte >> 4];
	out[1] = kDigits[byte & 0x0f];
}

}  // namespace w2b
