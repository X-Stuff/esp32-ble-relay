#include "BleUuid.h"

#include <cstring>

#include "HexText.h"

namespace w2b
{

namespace
{

// Byte groups of the 128-bit form: 8-4-4-4-12 hex digits.
constexpr size_t kGroupBytes[] = {4, 2, 2, 2, 6};

// the Bluetooth base UUID, 00000000-0000-1000-8000-00805f9b34fb, in written order
constexpr uint8_t kBase[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb};

bool readHexBytes(const char* text, uint8_t* out, size_t count)
{
	for (size_t i = 0; i < count; ++i)
	{
		if (!readHexByte(text + i * 2, out[i]))
		{
			return false;
		}
	}

	return true;
}

bool readLong(const char* text, uint8_t* out)
{
	size_t at = 0;
	uint8_t* next = out;

	for (size_t group = 0; group < 5; ++group)
	{
		if (group > 0 && text[at++] != '-')
		{
			return false;
		}

		if (!readHexBytes(text + at, next, kGroupBytes[group]))
		{
			return false;
		}

		at += kGroupBytes[group] * 2;
		next += kGroupBytes[group];
	}

	return true;
}

}  // namespace

BleUuid::BleUuid(const uint8_t* bytes, size_t size)
{
	if (bytes == nullptr || (size != 2 && size != 4 && size != 16))
	{
		return;
	}

	std::memcpy(value, bytes, size);
	length = static_cast<uint8_t>(size);
}

bool BleUuid::parse(const char* text, BleUuid& uuid)
{
	const size_t length = text != nullptr ? std::strlen(text) : 0;
	BleUuid read;

	if (length == 4 || length == 8)
	{
		read.length = static_cast<uint8_t>(length / 2);

		if (!readHexBytes(text, read.value, read.length))
		{
			return false;
		}
	}
	else if (length == Text::kCapacity)
	{
		read.length = 16;

		if (!readLong(text, read.value))
		{
			return false;
		}
	}
	else if (length != 0)
	{
		return false;
	}

	uuid = read;

	return true;
}

BleUuid::Text BleUuid::text() const
{
	char out[Text::kCapacity + 1] = {};
	size_t at = 0;

	if (length != 16)
	{
		for (size_t i = 0; i < length; ++i, at += 2)
		{
			writeHexByte(value[i], out + at);
		}

		return Text(out);
	}

	const uint8_t* next = value;

	for (size_t group = 0; group < 5; ++group)
	{
		if (group > 0)
		{
			out[at++] = '-';
		}

		for (size_t i = 0; i < kGroupBytes[group]; ++i, at += 2)
		{
			writeHexByte(*next++, out + at);
		}
	}

	return Text(out);
}

BleUuid BleUuid::to128Bit() const
{
	if (length == 16 || length == 0)
	{
		return *this;
	}

	uint8_t bytes[16];
	std::memcpy(bytes, kBase, sizeof(bytes));
	// a 16-bit UUID fills bytes 2 and 3, a 32-bit one bytes 0 to 3
	std::memcpy(bytes + 4 - length, value, length);

	return BleUuid(bytes, sizeof(bytes));
}

bool BleUuid::operator==(const BleUuid& other) const
{
	return length == other.length && std::memcmp(value, other.value, length) == 0;
}

}  // namespace w2b
