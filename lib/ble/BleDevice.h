#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "BleDeviceBase.h"
#include "BleUuid.h"
#include "FixedString.h"
#include "JsonForward.h"

namespace w2b
{

// One device found by a BLE scan, from its advertisements and scan responses: what a connect needs (BleDeviceBase) and what the
// page shows, the name, whether it takes a connect, the advertised services and the manufacturer data
class BleDevice : public BleDeviceBase
{
public:
	// the most advertised service UUIDs kept
	static constexpr size_t kMaxServices = 8;

	// the most bytes of one advertising data field: a 31-byte legacy packet less the field's length and type bytes
	static constexpr size_t kMaxFieldBytes = 29;

	// the advertised name
	using Name = FixedString<kMaxFieldBytes>;

	// the advertised service UUIDs, serviceCount() of them in use
	using Services = std::array<BleUuid, kMaxServices>;

	// the company id, low byte first, then the company's own bytes; manufacturerSize() of them in use
	using ManufacturerData = std::array<uint8_t, kMaxFieldBytes>;

	// type: of address, as advertised; connectable: the advertisement accepts a connect; rssi in dBm; name empty when none was
	// advertised; no services, no manufacturer data
	BleDevice(const BleAddress& address, AddressType type, bool connectable, const Name& name, int8_t rssi);

	// an advertisement of the device accepts a connect; beacons and many broadcasts do not
	inline bool isConnectable() const { return connectable; }

	// as advertised; empty: none
	inline const Name& name() const { return advertisedName; }

	// the advertised service UUIDs, the first serviceCount() in use
	inline const Services& services() const { return uuids; }

	// advertised service UUIDs kept
	inline size_t serviceCount() const { return uuidCount; }

	// the manufacturer data, the first manufacturerSize() bytes in use
	inline const ManufacturerData& manufacturerData() const { return makerBytes; }

	// manufacturer data bytes; 0: none advertised
	inline size_t manufacturerSize() const { return makerLength; }

	// Adds uuid unless it is none, listed already, or kMaxServices are.
	void addService(const BleUuid& uuid);

	// Keeps the first kMaxFieldBytes of the size bytes at data.
	void setManufacturerData(const uint8_t* data, size_t size);

	// Another report of the same device: the stronger signal; its name, services and manufacturer data where it carries them;
	// connectable when either is.
	void merge(const BleDevice& report);

	// BleDeviceBase's keys, then "connectable", "name", "services", "manufacturer_data" as hex, "" for none
	void toJson(JsonObject& json) const;

private:
	bool connectable;

	Name advertisedName;

	Services uuids{};

	uint8_t uuidCount = 0;

	ManufacturerData makerBytes{};

	uint8_t makerLength = 0;
};

}  // namespace w2b
