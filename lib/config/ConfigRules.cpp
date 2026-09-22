#include "ConfigRules.h"

#include <cstring>

namespace w2b
{

namespace
{

bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

bool isLetter(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isHexDigit(char c)
{
	return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

}  // namespace

bool isValidHostname(const char* name)
{
	const size_t length = std::strlen(name);

	if (length == 0 || length > 63 || name[0] == '-' || name[length - 1] == '-')
	{
		return false;
	}

	for (size_t i = 0; i < length; ++i)
	{
		if (!isLetter(name[i]) && !isDigit(name[i]) && name[i] != '-')
		{
			return false;
		}
	}

	return true;
}

bool isValidWifiPassword(const char* password)
{
	const size_t length = std::strlen(password);

	if (length == 0)
	{
		return true;
	}

	const bool hexKey = length == 64;

	if (!hexKey && (length < 8 || length > 63))
	{
		return false;
	}

	for (size_t i = 0; i < length; ++i)
	{
		const char c = password[i];
		const bool allowed = hexKey ? isHexDigit(c) : (c >= 0x20 && c <= 0x7e);

		if (!allowed)
		{
			return false;
		}
	}

	return true;
}

bool isValidSubnetMask(uint32_t mask)
{
	const uint32_t hostBits = ~mask;

	return mask != 0 && (hostBits & (hostBits + 1)) == 0;
}

bool parseIPv4(const char* text, uint32_t& address)
{
	uint32_t value = 0;
	const char* at = text;

	for (int part = 0; part < 4; ++part)
	{
		if (part > 0 && *at++ != '.')
		{
			return false;
		}

		const char* start = at;
		uint32_t number = 0;

		while (isDigit(*at) && at - start < 3)
		{
			number = number * 10 + static_cast<uint32_t>(*at - '0');
			++at;
		}

		if (at == start || number > 255)
		{
			return false;
		}

		value = value << 8 | number;
	}

	if (*at != '\0')
	{
		return false;
	}

	address = value;

	return true;
}

bool isValidRuleId(const char* id)
{
	const size_t length = std::strlen(id);

	if (length == 0 || length > 32)
	{
		return false;
	}

	for (size_t i = 0; i < length; ++i)
	{
		if (!isLetter(id[i]) && !isDigit(id[i]) && id[i] != '-' && id[i] != '_')
		{
			return false;
		}
	}

	return true;
}

bool isValidLabel(const char* text)
{
	for (const char* at = text; *at != '\0'; ++at)
	{
		const unsigned char byte = static_cast<unsigned char>(*at);

		if (byte < 0x20 || byte == 0x7f)
		{
			return false;
		}
	}

	return true;
}

}  // namespace w2b
