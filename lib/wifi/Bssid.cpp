#include "Bssid.h"

namespace w2b
{

BSSIDStr::BSSIDStr(const BSSID& bssid)
{
	constexpr char kHexDigits[] = "0123456789ABCDEF";

	size_t at = 0;

	for (size_t i = 0; i < bssid.size(); ++i)
	{
		if (i > 0)
		{
			text[at++] = ':';
		}

		text[at++] = kHexDigits[bssid[i] >> 4];
		text[at++] = kHexDigits[bssid[i] & 0x0f];
	}

	text[at] = '\0';
}

}  // namespace w2b
