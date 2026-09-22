#include "BatteryMonitor.h"

#include <algorithm>

#include <Arduino.h>

#include "Config.h"
#include "DeviceConfig.h"
#include "Logger.h"

namespace w2b
{

namespace
{

#ifdef BATTERY_GPIO
static_assert(BATTERY_DIVIDER_BOTTOM_KOHM > 0, "BATTERY_DIVIDER_BOTTOM_KOHM: the divider's lower resistor, not 0");

constexpr int kGpio = BATTERY_GPIO;

constexpr uint32_t kTopKOhm = BATTERY_DIVIDER_TOP_KOHM;

constexpr uint32_t kBottomKOhm = BATTERY_DIVIDER_BOTTOM_KOHM;
#else
// no battery pin on this board
constexpr int kGpio = -1;

constexpr uint32_t kTopKOhm = 0;

constexpr uint32_t kBottomKOhm = 1;
#endif

}  // namespace

std::atomic<uint32_t> BatteryMonitor::lastMillivolts{0};

std::atomic<uint32_t> BatteryMonitor::lastUncalibratedMillivolts{0};

std::atomic<uint32_t> BatteryMonitor::scalePpm{DeviceConfig::kUncalibratedBatteryScalePpm};

BatteryMonitor::BatteryMonitor(const Config& config)
	: config(config)
{
}

void BatteryMonitor::setScale(uint32_t ppm)
{
	scalePpm.store(ppm);
}

void BatteryMonitor::setup()
{
	if (kGpio < 0)
	{
		Logger::info("battery", "none: the board's env sets no BATTERY_GPIO");
		return;
	}

	// analogReadMilliVolts calibrates for the global attenuation, 11 dB by default; the pin must match it
	analogSetPinAttenuation(kGpio, ADC_11db);
	Logger::debug("battery", "GPIO%d through a %lu k / %lu k divider", kGpio, (unsigned long)kTopKOhm, (unsigned long)kBottomKOhm);

	setScale(config.device().batteryScalePpm());
	read();
	logVoltage();
}

void BatteryMonitor::update(uint32_t dtMs)
{
	if (kGpio < 0)
	{
		return;
	}

	read();
	sinceLogMs += dtMs;

	if (sinceLogMs >= kLogPeriodMs)
	{
		sinceLogMs = 0;
		logVoltage();
	}
}

void BatteryMonitor::read()
{
	samples[next] = (uint16_t)analogReadMilliVolts(kGpio);
	next = (next + 1) % kSamples;
	count = std::min(count + 1, kSamples);

	const uint32_t uncalibrated = pinSum() * (kTopKOhm + kBottomKOhm) / (count * kBottomKOhm);
	const uint32_t scaled = scaledMillivolts(uncalibrated);

	lastUncalibratedMillivolts.store(uncalibrated);
	lastMillivolts.store(scaled >= kNoBatteryBelowMv ? scaled : 0);
}

void BatteryMonitor::logVoltage() const
{
	const uint32_t scaled = scaledMillivolts(lastUncalibratedMillivolts.load());
	const uint32_t centivolts = (scaled + 5) / 10;

	Logger::debug("battery", "%s%lu.%02lu V, GPIO%d %lu mV", scaled >= kNoBatteryBelowMv ? "" : "no battery: ", (unsigned long)(centivolts / 100),
		(unsigned long)(centivolts % 100), kGpio, (unsigned long)(pinSum() / count));
}

uint32_t BatteryMonitor::pinSum() const
{
	uint32_t sum = 0;

	for (size_t i = 0; i < count; ++i)
	{
		sum += samples[i];
	}

	return sum;
}

uint32_t BatteryMonitor::scaledMillivolts(uint32_t uncalibrated) const
{
	return (uint32_t)((uint64_t)uncalibrated * scalePpm.load() / DeviceConfig::kUncalibratedBatteryScalePpm);
}

}  // namespace w2b
