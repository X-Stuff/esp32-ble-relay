// The characteristics found in a rule's service, and the TX and RX handles a rule uses on its connection, checked against the
// characteristics' properties.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "BleUuid.h"
#include "RuleConfig.h"

namespace w2b
{

// One characteristic as found by the connection's discovery
class DiscoveredCharacteristic
{
public:
	// valueHandle: the handle to read, write and subscribe; endHandle: its last handle, before the next characteristic
	DiscoveredCharacteristic(const BleUuid& uuid, uint8_t properties, uint16_t valueHandle, uint16_t endHandle);

	// its UUID, in the form the device sent
	inline const BleUuid& uuid() const { return id; }

	// the property bits: 0x08 write, 0x10 notify, 0x20 indicate and so on
	inline uint8_t properties() const { return bits; }

	// the value's handle
	inline uint16_t valueHandle() const { return handle; }

	// the characteristic's last handle; its descriptors are between valueHandle() and this
	inline uint16_t endHandle() const { return lastHandle; }

private:
	BleUuid id;

	uint8_t bits;

	uint16_t handle;

	uint16_t lastHandle;
};

// The handles a rule uses on its connection: TX and RX value handles, 0 when the rule has no such channel
class ChannelHandles
{
public:
	// Finds rule's TX and RX in characteristics (of the rule's service); false with an error like "tx fed5: ..." when one is missing or
	// lacks a needed property. TX writes with response when the rule asks and the characteristic allows it; RX subscribes as asked.
	static bool find(const RuleConfig& rule, const std::vector<DiscoveredCharacteristic>& characteristics, ChannelHandles& handles,
		std::string& error);

	// no TX, no RX
	ChannelHandles() = default;

	// TX's value handle; 0: no TX
	inline uint16_t txHandle() const { return tx; }

	// TX writes with response
	inline bool txWithResponse() const { return withResponse; }

	// RX's value handle; 0: no RX
	inline uint16_t rxHandle() const { return rx; }

	// RX's last handle: the CCCD search ends here
	inline uint16_t rxEnd() const { return rxEndHandle; }

	// true: RX subscribes to indications; false: to notifications
	inline bool rxIndicate() const { return indicate; }

	// same handles and modes
	bool operator==(const ChannelHandles& other) const;

private:
	static bool findTx(const RuleConfig& rule, const std::vector<DiscoveredCharacteristic>& characteristics, ChannelHandles& handles,
		std::string& error);

	static bool findRx(const RuleConfig& rule, const std::vector<DiscoveredCharacteristic>& characteristics, ChannelHandles& handles,
		std::string& error);

	uint16_t tx = 0;

	bool withResponse = false;

	uint16_t rx = 0;

	uint16_t rxEndHandle = 0;

	bool indicate = false;
};

}  // namespace w2b
