#pragma once

#include <cstdint>

namespace w2b
{

// A unit of periodic work: setup() runs once, then update(dtMs) repeatedly, at a pace the caller sets
class Component
{
public:
	virtual ~Component() = default;

	// runs once, before the first update()
	virtual void setup() = 0;

	// runs repeatedly; dtMs: the milliseconds elapsed since the previous call
	virtual void update(uint32_t dtMs) = 0;
};

}  // namespace w2b
