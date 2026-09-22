// Converts addresses and UUIDs between NimBLE (least significant byte first) and lib/ble (bytes in written order). Firmware only.
#pragma once

#include <string>

#include <NimBLEAddress.h>
#include <NimBLEUUID.h>

#include "BleAddress.h"
#include "BleDeviceBase.h"
#include "BleUuid.h"

namespace w2b
{

// NimBLE address to BleAddress
BleAddress toBleAddress(const NimBLEAddress& address);

// Public or random, as advertised; the identity types (only with privacy) by their random bit.
BleDeviceBase::AddressType toAddressType(const NimBLEAddress& address);

// BleAddress and its type to a NimBLE address, for a connect
NimBLEAddress toNimBleAddress(const BleAddress& address, BleDeviceBase::AddressType type);

// NimBLE UUID to BleUuid, keeping its size: 16, 32 or 128 bits
BleUuid toBleUuid(const NimBLEUUID& uuid);

// A NimBLE status for the log: "517, HCI 0x05 authentication failure"; HCI, pairing (SM) and ATT codes by kind, common ones named.
std::string describeNimBleError(int status);

}  // namespace w2b
