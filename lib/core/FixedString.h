#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace w2b
{

// Zero-terminated text of at most N bytes in a fixed block, no heap. A longer text is cut to N bytes, and fits() turns false.
template <size_t N>
class FixedString
{
public:
	static_assert(N < 256, "the length is held in one byte");

	// the most bytes it holds
	static constexpr size_t kCapacity = N;

	// empty
	FixedString() = default;

	// a copy of text, cut to N bytes; null is empty
	FixedString(const char* text)
	{
		assign(text);
	}

	// zero-terminated
	inline const char* c_str() const { return value; }

	// in bytes, without the terminator
	inline size_t size() const { return length; }

	// no text
	inline bool empty() const { return length == 0; }

	// false after an assignment longer than N bytes
	inline bool fits() const { return fit; }

	// copies text, cut to N bytes; null is empty
	void assign(const char* text)
	{
		const size_t full = text != nullptr ? std::strlen(text) : 0;
		length = static_cast<uint8_t>(full < N ? full : N);

		if (length > 0)
		{
			std::memcpy(value, text, length);
		}

		value[length] = '\0';
		fit = full <= N;
	}

	// same text, both fitting or both cut
	inline bool operator==(const FixedString& other) const { return fit == other.fit && std::strcmp(value, other.value) == 0; }

	// not the same text
	inline bool operator!=(const FixedString& other) const { return !(*this == other); }

	// same text as other; never for null
	inline bool operator==(const char* other) const { return other != nullptr && std::strcmp(value, other) == 0; }

	// not the same text as other
	inline bool operator!=(const char* other) const { return !(*this == other); }

	// readonly indexer
	inline const char& operator[](auto indx) const { return value[indx]; }

	// mutable indexer
	inline char& operator[](auto indx) { return value[indx]; }

private:
	char value[N + 1] = {};

	uint8_t length = 0;

	bool fit = true;
};

}  // namespace w2b
