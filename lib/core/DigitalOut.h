#pragma once

namespace w2b
{

// One output pin. The firmware implements it over an Arduino GPIO (lib/esp32/Esp32DigitalOut); tests use a fake that records the level.
class DigitalOut
{
public:
	virtual ~DigitalOut() = default;

	// configures the pin as an output
	virtual void setup() = 0;

	// drives the pin high or low
	virtual void write(bool high) = 0;
};

}  // namespace w2b
