#include "BleDeviceBase.h"

#include <algorithm>

#include <ArduinoJson.h>

#include "JsonMembers.h"

namespace w2b
{

BleDeviceBase::BleDeviceBase(const BleAddress& address, AddressType type, int8_t rssi)
	: peer(address), peerType(type), signal(rssi)
{
}

void BleDeviceBase::merge(const BleDeviceBase& report)
{
	signal = std::max(signal, report.signal);
}

void BleDeviceBase::toJson(JsonObject& json) const
{
	json["address"] = peer.text().c_str();
	json["address_type"] = choiceName(peerType, kAddressTypes);
	json["rssi"] = signal;
}

}  // namespace w2b
