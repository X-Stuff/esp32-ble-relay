#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Component.h"

namespace w2b
{

class Config;

// The battery's voltage on the board env's BATTERY_GPIO through its divider: the mean of the last kSamples readings, times the
// configuration's battery scale. Without BATTERY_GPIO it reads nothing and the voltage stays 0. One per board. Firmware only.
class BatteryMonitor : public Component
{
public:
	// pin readings the mean spans, one per update
	static constexpr size_t kSamples = 20;

	// a battery voltage below this reads as no battery: the ADC's floor through the divider, or a pin left open
	static constexpr uint32_t kNoBatteryBelowMv = 500;

	// the period of the debug line with the voltage
	static constexpr uint32_t kLogPeriodMs = 60000;

	// config: holds the battery scale, read by setup()
	explicit BatteryMonitor(const Config& config);

	// the battery's voltage from the mean, scaled; 0: no battery
	inline static uint32_t millivolts() { return lastMillivolts.load(); }

	// the battery's voltage from the mean through the divider alone: no scale, no kNoBatteryBelowMv
	inline static uint32_t uncalibratedMillivolts() { return lastUncalibratedMillivolts.load(); }

	// ppm: the configuration's battery_scale_ppm, in use from the next reading
	static void setScale(uint32_t ppm);

	// sets the pin's attenuation to 11 dB, the range up to about 2.45 V, reads the scale and takes the first reading
	void setup() override;

	// reads the pin once into the ring of the last kSamples readings and keeps the voltages of their mean
	void update(uint32_t dtMs) override;

private:
	void read();

	void logVoltage() const;

	uint32_t pinSum() const;

	uint32_t scaledMillivolts(uint32_t uncalibrated) const;

	static std::atomic<uint32_t> lastMillivolts;

	static std::atomic<uint32_t> lastUncalibratedMillivolts;

	// the configuration's battery scale, parts per million
	static std::atomic<uint32_t> scalePpm;

	const Config& config;

	// the pin's last readings, mV; the first `count` hold readings
	std::array<uint16_t, kSamples> samples{};

	// where the next reading goes
	size_t next = 0;

	size_t count = 0;

	uint32_t sinceLogMs = 0;
};

}  // namespace w2b
