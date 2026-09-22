#include "NimBleConvert.h"

#include <cstdio>

#include <NimBLEDevice.h>

namespace w2b
{

namespace
{

// the Bluetooth Core Specification's names of the HCI codes a connect or disconnect reports most
const char* hciName(int code)
{
	switch (code)
	{
		case BLE_ERR_AUTH_FAIL:           return "authentication failure";
		case BLE_ERR_PINKEY_MISSING:      return "key missing";
		case BLE_ERR_CONN_SPVN_TMO:       return "connection timeout";
		case BLE_ERR_REM_USER_CONN_TERM:  return "closed by the device";
		case BLE_ERR_RD_CONN_TERM_RESRCS: return "closed by the device, low resources";
		case BLE_ERR_RD_CONN_TERM_PWROFF: return "closed by the device, power off";
		case BLE_ERR_CONN_TERM_LOCAL:     return "closed by the relay";
		case BLE_ERR_LMP_LL_RSP_TMO:      return "link layer response timeout";
		case BLE_ERR_CONN_TERM_MIC:       return "encryption check failed";
		case BLE_ERR_CONN_ESTABLISHMENT:  return "connection failed to be established";
		default:                          return "";
	}
}

// the Bluetooth Core Specification's names of the pairing failure codes
const char* smName(int code)
{
	switch (code)
	{
		case BLE_SM_ERR_PASSKEY:          return "passkey entry failed";
		case BLE_SM_ERR_OOB:              return "OOB not available";
		case BLE_SM_ERR_AUTHREQ:          return "authentication requirements not met";
		case BLE_SM_ERR_CONFIRM_MISMATCH: return "confirm value failed";
		case BLE_SM_ERR_PAIR_NOT_SUPP:    return "pairing not supported";
		case BLE_SM_ERR_ENC_KEY_SZ:       return "encryption key size";
		case BLE_SM_ERR_CMD_NOT_SUPP:     return "command not supported";
		case BLE_SM_ERR_UNSPECIFIED:      return "unspecified reason";
		case BLE_SM_ERR_REPEATED:         return "repeated attempts";
		case BLE_SM_ERR_INVAL:            return "invalid parameters";
		case BLE_SM_ERR_DHKEY:            return "DHKey check failed";
		case BLE_SM_ERR_NUMCMP:           return "numeric comparison failed";
		case BLE_SM_ERR_ALREADY:          return "pairing in progress";
		case BLE_SM_ERR_KEY_REJ:          return "key rejected";
		default:                          return "";
	}
}

// the Bluetooth Core Specification's names of the ATT errors a read of a guarded value gets
const char* attName(int code)
{
	switch (code)
	{
		case BLE_ATT_ERR_READ_NOT_PERMITTED:  return "read not permitted";
		case BLE_ATT_ERR_INSUFFICIENT_AUTHEN: return "insufficient authentication";
		case BLE_ATT_ERR_INSUFFICIENT_AUTHOR: return "insufficient authorization";
		case BLE_ATT_ERR_ATTR_NOT_FOUND:      return "attribute not found";
		case BLE_ATT_ERR_INSUFFICIENT_ENC:    return "insufficient encryption";
		default:                              return "";
	}
}

}  // namespace

BleAddress toBleAddress(const NimBLEAddress& address)
{
	const uint8_t* value = address.getVal();
	BleAddress::Bytes bytes{};

	for (size_t i = 0; i < bytes.size(); ++i)
	{
		bytes[i] = value[bytes.size() - 1 - i];
	}

	return BleAddress(bytes);
}

BleDeviceBase::AddressType toAddressType(const NimBLEAddress& address)
{
	const uint8_t type = address.getType();

	return type == BLE_ADDR_RANDOM || type == BLE_ADDR_RANDOM_ID ? BleDeviceBase::AddressType::Random : BleDeviceBase::AddressType::Public;
}

// NimBLE takes the six bytes in written order here and turns them itself
NimBLEAddress toNimBleAddress(const BleAddress& address, BleDeviceBase::AddressType type)
{
	return NimBLEAddress(address.bytes().data(), type == BleDeviceBase::AddressType::Random ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC);
}

BleUuid toBleUuid(const NimBLEUUID& uuid)
{
	const uint8_t* value = uuid.getValue();
	const size_t size = uuid.bitSize() / 8;
	uint8_t bytes[16] = {};

	if (value == nullptr || size > sizeof(bytes))
	{
		return BleUuid();
	}

	for (size_t i = 0; i < size; ++i)
	{
		bytes[i] = value[size - 1 - i];
	}

	return BleUuid(bytes, size);
}

// NimBLE adds a base to a Bluetooth code: 0x100 ATT, 0x200 HCI, 0x400 pairing on the relay's side, 0x500 on the device's.
std::string describeNimBleError(int status)
{
	const char* kind = nullptr;
	const char* name = "";
	int code = 0;

	if (status >= BLE_HS_ERR_ATT_BASE && status < BLE_HS_ERR_HCI_BASE)
	{
		code = status - BLE_HS_ERR_ATT_BASE;
		kind = "ATT";
		name = attName(code);
	}
	else if (status >= BLE_HS_ERR_HCI_BASE && status < BLE_HS_ERR_L2C_BASE)
	{
		code = status - BLE_HS_ERR_HCI_BASE;
		kind = "HCI";
		name = hciName(code);
	}
	else if (status >= BLE_HS_ERR_SM_US_BASE && status < BLE_HS_ERR_HW_BASE)
	{
		const bool ours = status < BLE_HS_ERR_SM_PEER_BASE;
		code = status - (ours ? BLE_HS_ERR_SM_US_BASE : BLE_HS_ERR_SM_PEER_BASE);
		kind = ours ? "pairing, the relay's" : "pairing, the device's";
		name = smName(code);
	}

	if (kind == nullptr)
	{
		return std::to_string(status);
	}

	char text[96];
	snprintf(text, sizeof(text), "%d, %s 0x%02x%s%s", status, kind, code, name[0] != '\0' ? " " : "", name);

	return text;
}

}  // namespace w2b
