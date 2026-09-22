#include "Thread.h"

namespace w2b
{

Thread::Thread(const Settings& settings)
	: settings(settings)
{
}

Thread::~Thread()
{
	stop();
}

bool Thread::start(Body body)
{
	if (alive)
	{
		return false;
	}

	work = std::move(body);
	alive = true;

	TaskHandle_t created = nullptr;
	BaseType_t ok = xTaskCreatePinnedToCore(entry, settings.name, settings.stackBytes, this, settings.priority, &created, settings.core);

	if (ok != pdPASS)
	{
		alive = false;
		return false;
	}

	// the body may already have finished on the other core; then it cleared alive
	if (alive)
	{
		handle = created;
	}

	return true;
}

void Thread::stop()
{
	TaskHandle_t victim = handle.exchange(nullptr);
	alive = false;

	if (victim != nullptr)
	{
		// never returns when victim is the calling thread
		vTaskDelete(victim);
	}
}

bool Thread::isCurrent() const
{
	return alive && handle == xTaskGetCurrentTaskHandle();
}

// ESP-IDF's FreeRTOS counts the stack and its high water mark, the least ever free, in bytes.
size_t Thread::maxStackUsed() const
{
	const TaskHandle_t task = handle;

	return task != nullptr ? settings.stackBytes - uxTaskGetStackHighWaterMark(task) : 0;
}

void Thread::entry(void* arg)
{
	static_cast<Thread*>(arg)->run();
}

void Thread::run()
{
	handle = xTaskGetCurrentTaskHandle();
	work();

	handle = nullptr;
	alive = false;
	vTaskDelete(nullptr);
}

}  // namespace w2b
