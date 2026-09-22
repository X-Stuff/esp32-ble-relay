#include "Esp32DigitalOut.h"

#include <Arduino.h>

namespace w2b
{

Esp32DigitalOut::Esp32DigitalOut(uint8_t gpio)
	: gpio(gpio)
{
}

void Esp32DigitalOut::setup()
{
	pinMode(gpio, OUTPUT);
}

void Esp32DigitalOut::write(bool high)
{
	digitalWrite(gpio, high ? HIGH : LOW);
}

}  // namespace w2b
