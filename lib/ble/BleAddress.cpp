#include "BleAddress.h"

#include <cstring>

#include "HexText.h"

namespace w2b
{

bool BleAddress::parse(const char* text, BleAddress& address)
{
	Bytes bytes{};

	if (text == nullptr || std::strlen(text) != Text::kCapacity)
	{
		return false;
	}

	for (size_t i = 0; i < bytes.size(); ++i)
	{
		const char* part = text + i * 3;

		if (!readHexByte(part, bytes[i]) || (i + 1 < bytes.size() && part[2] != ':'))
		{
			return false;
		}
	}

	address.value = bytes;

	return true;
}

BleAddress::Text BleAddress::text() const
{
	char out[Text::kCapacity + 1] = {};

	for (size_t i = 0; i < value.size(); ++i)
	{
		writeHexByte(value[i], out + i * 3);

		if (i + 1 < value.size())
		{
			out[i * 3 + 2] = ':';
		}
	}

	return Text(out);
}

}  // namespace w2b
