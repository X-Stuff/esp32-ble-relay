#include "Heartbeat.h"

#include <Arduino.h>

#include "Config.h"
#include "LogRing.h"
#include "Logger.h"
#include "Scheduler.h"

namespace w2b
{

namespace
{

// bytes in tenths of a KB, for "%u.%u KB"
unsigned tenthsOfKb(size_t bytes)
{
	return (unsigned)(bytes * 10 / 1024);
}

}  // namespace

Heartbeat::Heartbeat(const Config& config, const LogRing& ring, const Scheduler& first, const Scheduler& second)
	: config(config), ring(ring), first(first), second(second)
{
}

void Heartbeat::setup()
{
}

void Heartbeat::update(uint32_t dtMs)
{
	const uint32_t periodMs = config.device().heartbeatS() * 1000u;

	if (periodMs == 0)
	{
		sinceLineMs = 0;
		return;
	}

	sinceLineMs += dtMs;

	if (sinceLineMs < periodMs)
	{
		return;
	}

	sinceLineMs = 0;
	logHealth();
}

void Heartbeat::logHealth()
{
	const auto firstMaxUsedStack = tenthsOfKb(first.maxStackUsed());
	const auto firstMaxAvailStack = tenthsOfKb(first.maxStackAvailable());

	const auto secondMaxUsedStack = tenthsOfKb(second.maxStackUsed());
	const auto secondMaxAvailStack = tenthsOfKb(second.maxStackAvailable());

	const auto logUsedBytes = tenthsOfKb(ring.size());

	Logger::debug("health", "Heap: avail: %u KB (min: %u KB), max alloc: %u KB; Core %d stack: %u.%u(%u.%u) KB; Core %d stack: %u.%u(%u.%u) KB; "
		"Log: used %u.%u KB, lines dropped: %lu",
		ESP.getFreeHeap() / 1024, ESP.getMinFreeHeap() / 1024, ESP.getMaxAllocHeap() / 1024,
		(int)first.core(),  firstMaxUsedStack / 10, firstMaxUsedStack % 10, firstMaxAvailStack / 10, firstMaxAvailStack % 10,
		(int)second.core(), secondMaxUsedStack / 10, secondMaxUsedStack % 10, secondMaxAvailStack / 10, secondMaxAvailStack % 10,
		logUsedBytes / 10, logUsedBytes % 10, (unsigned long)ring.droppedLines());
}

}  // namespace w2b
