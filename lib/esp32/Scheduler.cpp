#include "Scheduler.h"

#include <utility>

#include <Arduino.h>
#include <freertos/task.h>

namespace w2b
{

namespace
{

uint32_t clockMs()
{
	return millis();
}

}  // namespace

Scheduler::Scheduler(const Thread::Settings& settings)
	: TaskList(clockMs), thread(settings)
{
}

Scheduler& Scheduler::every(uint32_t periodMs, Task task)
{
	TaskList::every(periodMs, std::move(task));

	return *this;
}

Scheduler& Scheduler::once(uint32_t delayMs, Once task)
{
	TaskList::once(delayMs, std::move(task));

	return *this;
}

Scheduler& Scheduler::start()
{
	if (thread.isCurrent())
	{
		return *this;
	}

	stop();
	thread.start([this]
		{
			loop();
		});

	return *this;
}

void Scheduler::stop()
{
	if (thread.isCurrent())
	{
		threadLocked.unlock();
		thread.stop();
	}
	else
	{
		LockGuard lock(&threadLocked);
		thread.stop();
	}
}

void Scheduler::loop()
{
	while(true)
	{
		uint32_t sleepMs = 0;

		{
			LockGuard lock(&threadLocked);
			sleepMs = runOnce();
		}

		vTaskDelay(pdMS_TO_TICKS(sleepMs));
	}
}

}  // namespace w2b
