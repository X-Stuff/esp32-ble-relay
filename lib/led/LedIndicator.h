#pragma once

#include <cstdint>

#include "Component.h"
#include "DigitalOut.h"

namespace w2b
{

// A pattern of the status LED
enum class LedPattern : uint8_t
{
	// always off
	Off,

	// always on: BLE connected
	Solid,

	// on 500 ms, off 500 ms: captive portal / access point
	Blink1Hz,

	// on 100 ms, off 100 ms: WiFi connecting
	Blink5Hz,

	// on 50 ms every 3 s: WiFi up, no BLE
	Heartbeat3s,

	// two 100 ms blinks every 2 s: rule paused
	DoubleBlink,
};

// The status LED: plays a LedPattern, stepped by update(dtMs), inverted briefly by flicker(). Drives one GPIO. set() and flicker()
// may be called from a task other than update()'s: a torn write costs at most one glitch of one update period.
class LedIndicator : public Component
{
public:
	// how long flicker() inverts the pattern's level, marking traffic
	static constexpr uint32_t kFlickerMs = 50;

	// pin: the LED; activeLow: the LED is on while the pin is low
	LedIndicator(DigitalOut& pin, bool activeLow);

	// the pattern being played
	inline LedPattern pattern() const { return current; }

	// sets up the pin and writes the current level
	void setup() override;

	// advances the pattern and the flicker by dtMs and writes the level
	void update(uint32_t dtMs) override;

	// Switching to another pattern restarts its phase; the same pattern is a no-op.
	void set(LedPattern pattern);

	// inverts the pattern's level for kFlickerMs
	void flicker();

	// the level setup() and update() write: the pattern's, inverted while a flicker lasts
	bool isOn() const;

private:
	// the pattern's level at phaseMs
	bool patternOn() const;

	void writePin();

	DigitalOut& pin;

	bool activeLow;

	LedPattern current = LedPattern::Off;

	// position inside the pattern period
	uint32_t phaseMs = 0;

	// remaining inverted time
	uint32_t flickerLeftMs = 0;
};

}  // namespace w2b
