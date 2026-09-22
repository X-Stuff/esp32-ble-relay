#include "JsonText.h"

#include <ArduinoJson.h>

namespace w2b
{

std::string JsonText::writeObject(const std::function<void(JsonObject&)>& fill)
{
	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	fill(root);

	std::string text;
	// one allocation of the exact size, not a growth by doubling as the characters come
	text.reserve(measureJson(doc));
	serializeJson(doc, text);

	return text;
}

bool JsonText::readObject(const std::string& text, const std::function<bool(const JsonObjectConst&, std::string&)>& fill, std::string& error)
{
	JsonDocument doc;
	DeserializationError status = deserializeJson(doc, text);

	if (status)
	{
		error = std::string("not valid JSON: ") + status.c_str();
		return false;
	}

	if (!doc.is<JsonObjectConst>())
	{
		error = "not a JSON object";
		return false;
	}

	return fill(doc.as<JsonObjectConst>(), error);
}

}  // namespace w2b
