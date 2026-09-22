#pragma once

#include <cstdint>

#include "DigitalOut.h"

namespace w2b
{

// DigitalOut over an Arduino GPIO. Drives one GPIO. Firmware only.
class Esp32DigitalOut : public DigitalOut
{
public:
	// gpio: the pin number
	explicit Esp32DigitalOut(uint8_t gpio);

	// configures the pin as an output
	void setup() override;

	// drives the pin high or low
	void write(bool high) override;

private:
	uint8_t gpio;
};

}  // namespace w2b
