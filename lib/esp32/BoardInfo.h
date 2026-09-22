#pragma once

#include <cstdint>

#include <esp_mac.h>
#include <esp_partition.h>
#include <esp_system.h>

#include "FixedString.h"

namespace w2b
{

// The board's fixed facts, read once at the first use: chip, flash, PSRAM, the WiFi and BT MACs, the reset reason, the app slots.
// Logs the boot banner. Firmware only; used through static methods.
class BoardInfo
{
public:
	// the chip's name with its revision, "ESP32-D0WDQ6 rev 3.1"
	using ChipName = FixedString<47>;

	// a MAC address as text, "12:34:56:78:9A:BC"
	using MacText = FixedString<17>;

	// the chip's name with its revision
	static const ChipName& chipName();

	// the WiFi station's MAC, from the efuse; empty when it could not be read
	static const MacText& wifiMac();

	// the BT MAC, from the efuse; empty when it could not be read
	static const MacText& btMac();

	// why the board last started, in words
	static const char* resetReason();

	// the app slot the firmware runs from
	static const esp_partition_t* runningApp();

	// the app slot an update goes into: the other one
	static const esp_partition_t* updateApp();

	// the 6 bytes of mac as text
	static MacText macText(const uint8_t* mac);

	// Logs the boot banner under the tag "boot": chip, flash, MACs, reset reason, memory, partitions, SDK. Reads only.
	static void logBoot();

private:
	// reads every fact
	BoardInfo();

	// the one instance, made at the first use
	static const BoardInfo& instance();

	static const char* resetReasonName(esp_reset_reason_t reason);

	// the MAC of type from the efuse; empty when the read fails
	static MacText readMac(esp_mac_type_t type);

	static void logMac(const char* label, const MacText& mac);

	static void logMemory();

	void logChip() const;

	void logFlash() const;

	void logPartitions() const;

private:
	ChipName chip;

	uint8_t cores = 0;

	uint32_t cpuMhz = 0;

	// CHIP_FEATURE_* bits
	uint32_t features = 0;

	uint32_t flashBytes = 0;

	// 0 when the flash chip did not say
	uint32_t physicalFlashBytes = 0;

	uint32_t flashMhz = 0;

	uint32_t flashMode = 0;

	uint32_t psramBytes = 0;

	MacText stationMac;

	MacText bluetoothMac;

	esp_reset_reason_t reset = ESP_RST_UNKNOWN;

	const esp_partition_t* runningAppSlot = nullptr;

	const esp_partition_t* updateAppSlot = nullptr;

	// the data partition mounted as the file system; null when the table has none
	const esp_partition_t* dataSlot = nullptr;
};

}  // namespace w2b
