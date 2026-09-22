#pragma once

#include <cstdint>

#include "Component.h"

namespace w2b
{

class Config;
class LogRing;
class Scheduler;

// Logs a debug line under the tag "health" every heartbeat_s of the configuration (0: never): the heap, the most stack each
// scheduler's thread has used, the log ring's fill. Counts the time between its updates. Firmware only.
class Heartbeat : public Component
{
public:
	// config: holds the period; ring: its fill is logged; first, second: the schedulers, in the line's order
	Heartbeat(const Config& config, const LogRing& ring, const Scheduler& first, const Scheduler& second);

	// does nothing
	void setup() override;

	// counts dtMs and logs one health line once the configured period has passed
	void update(uint32_t dtMs) override;

private:
	void logHealth();

	const Config& config;

	const LogRing& ring;

	const Scheduler& first;

	const Scheduler& second;

	// since the last line
	uint32_t sinceLineMs = 0;
};

}  // namespace w2b
