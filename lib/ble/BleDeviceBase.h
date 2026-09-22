#pragma once

#include <cstdint>

#include "BleAddress.h"
#include "JsonForward.h"

namespace w2b
{

// A BLE device as a connect needs it: its address, the kind of address and the signal. The base of BleDevice, a scan's report, and
// all a search keeps of the device it found.
class BleDeviceBase
{
public:
	// the address types' names in the JSON, in AddressType's order
	static constexpr const char* kAddressTypes[] = {"public", "random"};

	// The kind of address the advertisement carries, its TxAdd bit; a connect needs it with the address
	enum class AddressType : uint8_t
	{
		// assigned to the device's maker
		Public,

		// chosen by the device: static, or private and changing
		Random,
	};

	// type: of address, as advertised; rssi in dBm
	BleDeviceBase(const BleAddress& address, AddressType type, int8_t rssi);

	// the device's address
	inline const BleAddress& address() const { return peer; }

	// the kind of address, as advertised
	inline AddressType addressType() const { return peerType; }

	// dBm, the strongest received
	inline int8_t rssi() const { return signal; }

	// Another report of the same device: keeps the stronger signal.
	void merge(const BleDeviceBase& report);

	// "address", "address_type" (kAddressTypes), "rssi"
	void toJson(JsonObject& json) const;

private:
	BleAddress peer;

	AddressType peerType;

	int8_t signal;
};

}  // namespace w2b
