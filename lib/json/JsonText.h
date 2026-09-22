#pragma once

#include <functional>
#include <string>

#include "JsonSerializable.h"

namespace w2b
{

// The text form of a serializable class, a JSON document holding one object; the ArduinoJson work is in JsonText.cpp
class JsonText
{
public:
	// value as compact JSON
	template <typename T>
	static std::string write(const IJsonSerializable<T>& value)
	{
		return writeObject([&value](JsonObject& json)
			{
				value.toJson(json);
			});
	}

	// Reads text over target's current values through a copy of target on the stack; target is untouched on failure. Errors:
	// "not valid JSON: ...", "not a JSON object", or the one fromJson reported.
	template <typename T>
	static bool read(const std::string& text, T& target, std::string& error)
	{
		T parsed = target;

		if (!readInto(text, parsed, error))
		{
			return false;
		}

		target = parsed;

		return true;
	}

	// As read(), in place, without the copy: on failure target may hold part of the text.
	template <typename T>
	static bool readInto(const std::string& text, IJsonSerializable<T>& target, std::string& error)
	{
		auto fill = [&target](const JsonObjectConst& json, std::string& reason)
		{
			return target.fromJson(json, reason);
		};

		return readObject(text, fill, error);
	}

private:
	static std::string writeObject(const std::function<void(JsonObject&)>& fill);

	static bool readObject(const std::string& text, const std::function<bool(const JsonObjectConst&, std::string&)>& fill, std::string& error);
};

}  // namespace w2b
