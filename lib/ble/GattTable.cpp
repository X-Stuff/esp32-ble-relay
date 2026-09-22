#include "GattTable.h"

#include <algorithm>
#include <cstring>

#include <ArduinoJson.h>

#include "HexText.h"

namespace w2b
{

GattCharacteristic::GattCharacteristic(const BleUuid& uuid, uint8_t properties)
	: id(uuid), bits(properties)
{
}

void GattCharacteristic::toJson(JsonObject& json) const
{
	json["uuid"] = id.text().c_str();
	json["properties"] = bits;
}

GattService::GattService(const BleUuid& uuid)
	: id(uuid)
{
}

void GattService::add(const GattCharacteristic& characteristic)
{
	members.push_back(characteristic);
}

void GattService::toJson(JsonObject& json) const
{
	json["uuid"] = id.text().c_str();

	JsonArray list = json["characteristics"].to<JsonArray>();

	for (const GattCharacteristic& characteristic : members)
	{
		JsonObject item = list.add<JsonObject>();
		characteristic.toJson(item);
	}
}

GattValue::GattValue(const BleUuid& characteristic, const uint8_t* data, size_t size)
	: id(characteristic), length(static_cast<uint8_t>(data != nullptr ? std::min(size, kMaxBytes) : 0))
{
	if (length > 0)
	{
		std::memcpy(value.data(), data, length);
	}
}

void GattValue::toJson(JsonObject& json) const
{
	char hex[kMaxBytes * 2 + 1] = {};

	for (size_t i = 0; i < length; ++i)
	{
		writeHexByte(value[i], hex + i * 2);
	}

	json["uuid"] = id.text().c_str();
	json["value"] = hex;
}

void GattTable::add(const GattService& service)
{
	primary.push_back(service);
}

void GattTable::add(const GattValue& value)
{
	read.push_back(value);
}

size_t GattTable::characteristicCount() const
{
	size_t count = 0;

	for (const GattService& service : primary)
	{
		count += service.characteristics().size();
	}

	return count;
}

void GattTable::toJson(JsonObject& json) const
{
	JsonArray services = json["services"].to<JsonArray>();

	for (const GattService& service : primary)
	{
		JsonObject item = services.add<JsonObject>();
		service.toJson(item);
	}

	JsonArray values = json["values"].to<JsonArray>();

	for (const GattValue& value : read)
	{
		JsonObject item = values.add<JsonObject>();
		value.toJson(item);
	}
}

}  // namespace w2b
