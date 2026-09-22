// A device's GATT table as a discovery connection reads it: the primary services, each with its characteristics' UUIDs and
// property bits, and the values read of a few. Not persisted: toJson only, for the HTTP API.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "BleUuid.h"
#include "JsonForward.h"

namespace w2b
{

// One characteristic: its UUID and the property bits of its declaration
class GattCharacteristic
{
public:
	// properties: 0x01 broadcast, 0x02 read, 0x04 write without response, 0x08 write, 0x10 notify, 0x20 indicate, 0x40 authenticated
	// signed writes, 0x80 extended properties
	GattCharacteristic(const BleUuid& uuid, uint8_t properties);

	// the characteristic's UUID
	inline const BleUuid& uuid() const { return id; }

	// the declaration's property bits
	inline uint8_t properties() const { return bits; }

	// "uuid", "properties" as a number
	void toJson(JsonObject& json) const;

private:
	BleUuid id;

	uint8_t bits;
};

// One primary service with its characteristics in handle order
class GattService
{
public:
	// no characteristics
	explicit GattService(const BleUuid& uuid);

	// the service's UUID
	inline const BleUuid& uuid() const { return id; }

	// in handle order
	inline const std::vector<GattCharacteristic>& characteristics() const { return members; }

	// Appends characteristic, the next in handle order.
	void add(const GattCharacteristic& characteristic);

	// "uuid", "characteristics": [...]
	void toJson(JsonObject& json) const;

private:
	BleUuid id;

	std::vector<GattCharacteristic> members;
};

// A characteristic's value as read, its first kMaxBytes at most
class GattValue
{
public:
	// the most bytes kept of a value
	static constexpr size_t kMaxBytes = 64;

	// the value's bytes, size() of them in use
	using Bytes = std::array<uint8_t, kMaxBytes>;

	// characteristic: whose value; the first kMaxBytes of the size bytes at data
	GattValue(const BleUuid& characteristic, const uint8_t* data, size_t size);

	// the characteristic's UUID
	inline const BleUuid& uuid() const { return id; }

	// the value, the first size() bytes in use
	inline const Bytes& bytes() const { return value; }

	// bytes kept
	inline size_t size() const { return length; }

	// "uuid", "value" as hex
	void toJson(JsonObject& json) const;

private:
	BleUuid id;

	Bytes value{};

	uint8_t length = 0;
};

// The primary services of a device in handle order, and the values read of their characteristics; empty until a discovery read them
class GattTable
{
public:
	// in handle order
	inline const std::vector<GattService>& services() const { return primary; }

	// in the order they were read
	inline const std::vector<GattValue>& values() const { return read; }

	// Appends service, the next in handle order.
	void add(const GattService& service);

	// Appends a value read.
	void add(const GattValue& value);

	// the characteristics of every service
	size_t characteristicCount() const;

	// "services": [...], "values": [...]
	void toJson(JsonObject& json) const;

private:
	std::vector<GattService> primary;

	std::vector<GattValue> read;
};

}  // namespace w2b
