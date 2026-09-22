// Member readers and writers for toJson/fromJson; readers report "key: reason". Include from .cpp files only: it pulls in ArduinoJson.
#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include "FixedString.h"
#include "JsonSerializable.h"

namespace w2b
{

// true when the key is absent; an explicit null is present
bool isMissing(const JsonObjectConst& json, const char* key);

// Required string member; value points into the JSON document. "key: missing" or "key: expected a string".
bool readText(const JsonObjectConst& json, const char* key, const char*& value, std::string& error);

// Optional string member; missing or null keeps value, anything else as readText.
bool readOptionalText(const JsonObjectConst& json, const char* key, const char*& value, std::string& error);

// Required whole number member in 0..4294967295.
bool readUint(const JsonObjectConst& json, const char* key, uint32_t& value, std::string& error);

// Optional true/false member; missing or null keeps value.
bool readOptionalBool(const JsonObjectConst& json, const char* key, bool& value, std::string& error);

// Required string member of at most N bytes; a longer one fails with "key: longer than N bytes".
template <size_t N>
bool readString(const JsonObjectConst& json, const char* key, FixedString<N>& value, std::string& error)
{
	const char* text = nullptr;

	if (!readText(json, key, text, error))
	{
		return false;
	}

	if (std::strlen(text) > N)
	{
		error = std::string(key) + ": longer than " + std::to_string(N) + " bytes";
		return false;
	}

	value.assign(text);

	return true;
}

// Optional string member of at most N bytes; missing or null keeps value, anything else as readString.
template <size_t N>
bool readOptionalString(const JsonObjectConst& json, const char* key, FixedString<N>& value, std::string& error)
{
	if (json[key].isNull())
	{
		return true;
	}

	return readString(json, key, value, error);
}

// Optional whole number member that fits T (uint8_t, uint16_t, uint32_t); missing or null keeps value. The range a field allows is
// its class's validate() business.
template <typename T>
bool readOptionalUint(const JsonObjectConst& json, const char* key, T& value, std::string& error)
{
	JsonVariantConst member = json[key];

	if (member.isNull())
	{
		return true;
	}

	if (!member.is<T>())
	{
		error = std::string(key) + ": expected a whole number from 0 to " + std::to_string(std::numeric_limits<T>::max());
		return false;
	}

	value = member.as<T>();

	return true;
}

// Name of an enum value; choices lists the names in the order of the values.
template <typename E, size_t N>
const char* choiceName(E value, const char* const (&choices)[N])
{
	const size_t index = static_cast<size_t>(value);

	return index < N ? choices[index] : "?";
}

// Optional member naming an enum value, choices as for choiceName; missing or null keeps value. Another name fails with
// "key: \"x\" is not one of a, b, c".
template <typename E, size_t N>
bool readOptionalChoice(const JsonObjectConst& json, const char* key, E& value, const char* const (&choices)[N], std::string& error)
{
	const char* name = choiceName(value, choices);

	if (!readOptionalText(json, key, name, error))
	{
		return false;
	}

	for (size_t i = 0; i < N; ++i)
	{
		if (std::strcmp(name, choices[i]) == 0)
		{
			value = static_cast<E>(i);
			return true;
		}
	}

	error = std::string(key) + ": \"" + name + "\" is not one of ";

	for (size_t i = 0; i < N; ++i)
	{
		error += (i == 0 ? "" : ", ") + std::string(choices[i]);
	}

	return false;
}

// Required object member, read by target.fromJson; a nested error gets the key in front: "wifi.static.gateway: missing".
template <typename T>
bool readObject(const JsonObjectConst& json, const char* key, IJsonSerializable<T>& target, std::string& error)
{
	JsonVariantConst member = json[key];

	if (!member.is<JsonObjectConst>())
	{
		error = std::string(key) + (member.isNull() ? ": missing" : ": expected an object");
		return false;
	}

	if (!target.fromJson(member.as<JsonObjectConst>(), error))
	{
		error = std::string(key) + "." + error;
		return false;
	}

	return true;
}

// Optional object member; missing or null keeps target, anything else as readObject.
template <typename T>
bool readOptionalObject(const JsonObjectConst& json, const char* key, IJsonSerializable<T>& target, std::string& error)
{
	if (json[key].isNull())
	{
		return true;
	}

	return readObject(json, key, target, error);
}

// Writes value as a nested object under key.
template <typename T>
void writeObject(JsonObject& json, const char* key, const IJsonSerializable<T>& value)
{
	JsonObject member = json[key].to<JsonObject>();
	value.toJson(member);
}

}  // namespace w2b
