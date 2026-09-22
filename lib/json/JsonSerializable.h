#pragma once

#include <string>
#include <type_traits>

#include "JsonForward.h"

namespace w2b
{

// A class that is one JSON object; errors read "field: reason", the parent putting its key in front ("wifi.static.gateway: ...")
template <typename TSelf>
class IJsonSerializable
{
public:
	virtual ~IJsonSerializable() = default;

	// Writes every field into json, the object at the class's own nesting level.
	virtual void toJson(JsonObject& json) const = 0;

	// Reads the fields of json over the current values, then checks the result; false with the first problem in error.
	virtual bool fromJson(const JsonObjectConst& json, std::string& error) = 0;
};

// Creates the empty object for fromJson of a class whose default constructor is private and declares this class its friend
class SerializableFactory
{
public:
	// the empty T, for fromJson only
	template <typename T>
	static T createEmpty()
	{
		static_assert(std::is_base_of<IJsonSerializable<T>, T>::value, "createEmpty is only for classes implementing IJsonSerializable<T>");

		return T();
	}
};

}  // namespace w2b
