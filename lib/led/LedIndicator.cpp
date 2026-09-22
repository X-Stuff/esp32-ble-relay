#include "LedIndicator.h"

namespace w2b
{

namespace
{

// Alternating on/off durations in ms, starting with "on"; repeats forever.
struct Shape
{
	const uint16_t* segments;

	// 0 = constant level
	uint8_t count;

	// the constant level when count == 0
	bool level;
};

const uint16_t kBlink1Hz[] = {500, 500};
const uint16_t kBlink5Hz[] = {100, 100};
const uint16_t kHeartbeat3s[] = {50, 2950};
const uint16_t kDoubleBlink[] = {100, 100, 100, 1700};

// Indexed by LedPattern.
const Shape kShapes[] =
{
	// Off
	Shape
	{
		.segments = nullptr,
		.count = 0,
		.level = false
	},
	// Solid
	Shape
	{
		.segments = nullptr,
		.count = 0,
		.level = true
	},
	// Blink1Hz
	Shape
	{
		.segments = kBlink1Hz,
		.count = 2,
		.level = false
	},
	// Blink5Hz
	Shape
	{
		.segments = kBlink5Hz,
		.count = 2,
		.level = false
	},
	// Heartbeat3s
	Shape
	{
		.segments = kHeartbeat3s,
		.count = 2,
		.level = false
	},
	// DoubleBlink
	Shape
	{
		.segments = kDoubleBlink,
		.count = 4,
		.level = false
	}
};

const Shape& shapeOf(LedPattern pattern)
{
	return kShapes[static_cast<uint8_t>(pattern)];
}

uint32_t periodOf(const Shape& shape)
{
	uint32_t period = 0;

	for (uint8_t i = 0; i < shape.count; ++i)
	{
		period += shape.segments[i];
	}

	return period;
}

}  // namespace

LedIndicator::LedIndicator(DigitalOut& pin, bool activeLow)
	: pin(pin), activeLow(activeLow)
{
}

void LedIndicator::setup()
{
	pin.setup();
	writePin();
}

void LedIndicator::update(uint32_t dtMs)
{
	uint32_t period = periodOf(shapeOf(current));

	if (period > 0)
	{
		phaseMs = (phaseMs + dtMs) % period;
	}

	flickerLeftMs = flickerLeftMs > dtMs ? flickerLeftMs - dtMs : 0;

	writePin();
}

void LedIndicator::set(LedPattern pattern)
{
	if (pattern == current)
	{
		return;
	}

	phaseMs = 0;
	current = pattern;
}

void LedIndicator::flicker()
{
	flickerLeftMs = kFlickerMs;
}

bool LedIndicator::isOn() const
{
	return patternOn() != (flickerLeftMs > 0);
}

bool LedIndicator::patternOn() const
{
	const Shape& shape = shapeOf(current);

	if (shape.count == 0)
	{
		return shape.level;
	}

	uint32_t t = phaseMs;

	for (uint8_t i = 0; i < shape.count; ++i)
	{
		if (t < shape.segments[i])
		{
			return (i % 2) == 0;
		}

		t -= shape.segments[i];
	}

	return false;
}

void LedIndicator::writePin()
{
	pin.write(isOn() != activeLow);
}

}  // namespace w2b
