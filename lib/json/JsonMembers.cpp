#include "JsonMembers.h"

namespace w2b
{

bool isMissing(const JsonObjectConst& json, const char* key)
{
	return json[key].isUnbound();
}

bool readText(const JsonObjectConst& json, const char* key, const char*& value, std::string& error)
{
	JsonVariantConst member = json[key];

	if (member.isNull())
	{
		error = std::string(key) + ": missing";
		return false;
	}

	if (!member.is<const char*>())
	{
		error = std::string(key) + ": expected a string";
		return false;
	}

	value = member.as<const char*>();

	return true;
}

bool readOptionalText(const JsonObjectConst& json, const char* key, const char*& value, std::string& error)
{
	if (json[key].isNull())
	{
		return true;
	}

	return readText(json, key, value, error);
}

bool readUint(const JsonObjectConst& json, const char* key, uint32_t& value, std::string& error)
{
	JsonVariantConst member = json[key];

	if (member.isNull())
	{
		error = std::string(key) + ": missing";
		return false;
	}

	if (!member.is<uint32_t>())
	{
		error = std::string(key) + ": expected a whole number";
		return false;
	}

	value = member.as<uint32_t>();

	return true;
}

bool readOptionalBool(const JsonObjectConst& json, const char* key, bool& value, std::string& error)
{
	JsonVariantConst member = json[key];

	if (member.isNull())
	{
		return true;
	}

	if (!member.is<bool>())
	{
		error = std::string(key) + ": expected true or false";
		return false;
	}

	value = member.as<bool>();

	return true;
}

}  // namespace w2b
