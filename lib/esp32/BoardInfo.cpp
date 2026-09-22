#include "BoardInfo.h"

#include <cstdio>

#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>

#include "Logger.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "boot";

}  // namespace

BoardInfo::BoardInfo()
{
	esp_chip_info_t info;
	esp_chip_info(&info);
	cores = info.cores;
	features = info.features;
	cpuMhz = ESP.getCpuFreqMHz();

	// Arduino 3.x reports the revision as major*100 + minor (301 = v3.1)
	const uint32_t rev = ESP.getChipRevision();
	char text[ChipName::kCapacity + 1];

	if (rev >= 100)
	{
		snprintf(text, sizeof(text), "%s rev %u.%u", ESP.getChipModel(), (unsigned)(rev / 100), (unsigned)(rev % 100));
	}
	else
	{
		snprintf(text, sizeof(text), "%s rev %u", ESP.getChipModel(), (unsigned)rev);
	}

	chip.assign(text);

	uint32_t physical = 0;
	flashBytes = ESP.getFlashChipSize();
	physicalFlashBytes = esp_flash_get_physical_size(nullptr, &physical) == ESP_OK ? physical : 0;
	flashMhz = ESP.getFlashChipSpeed() / 1000000;
	flashMode = ESP.getFlashChipMode();
	psramBytes = ESP.getPsramSize();

	stationMac = readMac(ESP_MAC_WIFI_STA);
	bluetoothMac = readMac(ESP_MAC_BT);
	reset = esp_reset_reason();

	runningAppSlot = esp_ota_get_running_partition();
	updateAppSlot = esp_ota_get_next_update_partition(nullptr);
	dataSlot = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
}

const BoardInfo& BoardInfo::instance()
{
	static const BoardInfo board;

	return board;
}

const BoardInfo::ChipName& BoardInfo::chipName()
{
	return instance().chip;
}

const BoardInfo::MacText& BoardInfo::wifiMac()
{
	return instance().stationMac;
}

const BoardInfo::MacText& BoardInfo::btMac()
{
	return instance().bluetoothMac;
}

const char* BoardInfo::resetReason()
{
	return resetReasonName(instance().reset);
}

const esp_partition_t* BoardInfo::runningApp()
{
	return instance().runningAppSlot;
}

const esp_partition_t* BoardInfo::updateApp()
{
	return instance().updateAppSlot;
}

BoardInfo::MacText BoardInfo::macText(const uint8_t* mac)
{
	char text[MacText::kCapacity + 1];
	snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	return MacText(text);
}

void BoardInfo::logBoot()
{
	const BoardInfo& board = instance();

	board.logChip();
	board.logFlash();
	logMac("mac sta", board.stationMac);
	logMac("mac bt", board.bluetoothMac);
	Logger::info(kTag, "reset reason %s (%d)", resetReasonName(board.reset), (int)board.reset);
	logMemory();
	board.logPartitions();
	Logger::info(kTag, "sdk arduino %s on esp-idf %s", ESP.getCoreVersion(), ESP.getSdkVersion());
}

const char* BoardInfo::resetReasonName(esp_reset_reason_t reason)
{
	switch (reason)
	{
		case ESP_RST_POWERON:    return "power-on";
		case ESP_RST_EXT:        return "external reset pin";
		case ESP_RST_SW:         return "software (esp_restart)";
		case ESP_RST_PANIC:      return "panic / exception";
		case ESP_RST_INT_WDT:    return "interrupt watchdog";
		case ESP_RST_TASK_WDT:   return "task watchdog";
		case ESP_RST_WDT:        return "other watchdog";
		case ESP_RST_DEEPSLEEP:  return "deep-sleep wake";
		case ESP_RST_BROWNOUT:   return "brownout";
		case ESP_RST_SDIO:       return "SDIO";
		case ESP_RST_USB:        return "USB peripheral";
		case ESP_RST_JTAG:       return "JTAG";
		case ESP_RST_EFUSE:      return "efuse error";
		case ESP_RST_PWR_GLITCH: return "power glitch";
		case ESP_RST_CPU_LOCKUP: return "CPU lock-up";
		case ESP_RST_UNKNOWN:
		default:                 return "unknown";
	}
}

BoardInfo::MacText BoardInfo::readMac(esp_mac_type_t type)
{
	uint8_t mac[6] = {};

	return esp_read_mac(mac, type) == ESP_OK ? macText(mac) : MacText();
}

void BoardInfo::logMac(const char* label, const MacText& mac)
{
	if (mac.empty())
	{
		Logger::warn(kTag, "%s read failed", label);
		return;
	}

	Logger::info(kTag, "%s %s", label, mac.c_str());
}

void BoardInfo::logMemory()
{
	Logger::info(kTag, "heap free %u KB of %u KB, min free %u KB, largest block %u KB", ESP.getFreeHeap() / 1024, ESP.getHeapSize() / 1024,
		ESP.getMinFreeHeap() / 1024, ESP.getMaxAllocHeap() / 1024);
	Logger::info(kTag, "sketch %u KB used, %u KB free", ESP.getSketchSize() / 1024, ESP.getFreeSketchSpace() / 1024);
}

void BoardInfo::logPartitions() const
{
	if (runningAppSlot != nullptr)
	{
		Logger::info(kTag, "partition app %s @ 0x%06x, %u KB", runningAppSlot->label, (unsigned)runningAppSlot->address,
			(unsigned)(runningAppSlot->size / 1024));
	}

	if (dataSlot != nullptr)
	{
		Logger::info(kTag, "partition data %s @ 0x%06x, %u KB", dataSlot->label, (unsigned)dataSlot->address, (unsigned)(dataSlot->size / 1024));
	}
	else
	{
		Logger::warn(kTag, "no data/spiffs partition found");
	}
}

void BoardInfo::logChip() const
{
	Logger::info(kTag, "chip %s, %u core(s) @ %u MHz", chip.c_str(), (unsigned)cores, (unsigned)cpuMhz);
	Logger::info(kTag, "features %s%s%s%s", (features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "", (features & CHIP_FEATURE_BT) ? "BT " : "",
		(features & CHIP_FEATURE_BLE) ? "BLE " : "", (features & CHIP_FEATURE_EMB_FLASH) ? "embedded-flash" : "");
}

void BoardInfo::logFlash() const
{
	if (physicalFlashBytes > 0)
	{
		Logger::info(kTag, "flash %u MB configured, %u MB physical, %u MHz, mode %u", (unsigned)(flashBytes / (1024 * 1024)),
			(unsigned)(physicalFlashBytes / (1024 * 1024)), (unsigned)flashMhz, (unsigned)flashMode);
	}
	else
	{
		Logger::info(kTag, "flash %u MB configured, physical size unknown, %u MHz, mode %u", (unsigned)(flashBytes / (1024 * 1024)),
			(unsigned)flashMhz, (unsigned)flashMode);
	}

	Logger::info(kTag, "psram %u KB", (unsigned)(psramBytes / 1024));
}

}  // namespace w2b
