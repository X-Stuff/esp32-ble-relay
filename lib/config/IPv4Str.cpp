#include "IPv4Str.h"

namespace w2b
{

IPv4Str::IPv4Str(uint32_t address)
{
	size_t at = 0;

	for (int shift = 24; shift >= 0; shift -= 8)
	{
		if (shift < 24)
		{
			text[at++] = '.';
		}

		at = writeOctet(at, static_cast<uint8_t>(address >> shift));
	}

	text[at] = '\0';
}

size_t IPv4Str::writeOctet(size_t at, uint8_t octet)
{
	if (octet >= 100)
	{
		text[at++] = static_cast<char>('0' + octet / 100);
	}

	if (octet >= 10)
	{
		text[at++] = static_cast<char>('0' + octet / 10 % 10);
	}

	text[at++] = static_cast<char>('0' + octet % 10);

	return at;
}

}  // namespace w2b
