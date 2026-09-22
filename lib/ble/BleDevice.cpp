#include "BleDevice.h"

#include <algorithm>
#include <cstring>

#include <ArduinoJson.h>

#include "HexText.h"

namespace w2b
{

BleDevice::BleDevice(const BleAddress& address, AddressType type, bool connectable, const Name& name, int8_t rssi)
	: BleDeviceBase(address, type, rssi), connectable(connectable), advertisedName(name)
{
}

void BleDevice::addService(const BleUuid& uuid)
{
	if (uuid.empty() || uuidCount == kMaxServices || std::find(uuids.begin(), uuids.begin() + uuidCount, uuid) != uuids.begin() + uuidCount)
	{
		return;
	}

	uuids[uuidCount++] = uuid;
}

void BleDevice::setManufacturerData(const uint8_t* data, size_t size)
{
	makerLength = static_cast<uint8_t>(data != nullptr ? std::min(size, kMaxFieldBytes) : 0);

	if (makerLength > 0)
	{
		std::memcpy(makerBytes.data(), data, makerLength);
	}
}

void BleDevice::merge(const BleDevice& report)
{
	BleDeviceBase::merge(report);
	// a scan response that came without its advertisement is reported as not connectable
	connectable = connectable || report.connectable;

	if (!report.advertisedName.empty())
	{
		advertisedName = report.advertisedName;
	}

	for (size_t i = 0; i < report.uuidCount; ++i)
	{
		addService(report.uuids[i]);
	}

	if (report.makerLength > 0)
	{
		setManufacturerData(report.makerBytes.data(), report.makerLength);
	}
}

void BleDevice::toJson(JsonObject& json) const
{
	BleDeviceBase::toJson(json);
	json["connectable"] = connectable;
	json["name"] = advertisedName.c_str();

	JsonArray list = json["services"].to<JsonArray>();

	for (size_t i = 0; i < uuidCount; ++i)
	{
		list.add(uuids[i].text().c_str());
	}

	char hex[kMaxFieldBytes * 2 + 1] = {};

	for (size_t i = 0; i < makerLength; ++i)
	{
		writeHexByte(makerBytes[i], hex + i * 2);
	}

	json["manufacturer_data"] = hex;
}

}  // namespace w2b
