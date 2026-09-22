#pragma once

#include "TaskList.h"
#include "Thread.h"
#include "FreeRtosMutex.h"

namespace w2b
{

// A TaskList with its own FreeRTOS thread pinned to a core: the thread runs the due tasks and sleeps until the next one is due.
// Schedule before start() or from tasks running on this scheduler. Firmware only.
class Scheduler : public TaskList
{
public:
	// settings: the thread's name, stack, priority and core
	explicit Scheduler(const Thread::Settings& settings);

	// true while the thread exists
	inline bool running() const { return thread.running(); }

	// the thread's core
	inline BaseType_t core() const { return thread.core(); }

	// the most stack the thread has used since it started, in bytes; 0 while it does not run
	inline size_t maxStackUsed() const { return thread.maxStackUsed(); }

	// maximum available bytes in stack of the thread
	inline uint32_t maxStackAvailable() const { return thread.stackBytes(); }

	// TaskList::every, returning the Scheduler, so a chain schedules every task and ends with start()
	Scheduler& every(uint32_t periodMs, Task task);

	// TaskList::once, returning the Scheduler for the same chain
	Scheduler& once(uint32_t delayMs, Once task);

	// Starts the thread; a running scheduler is stopped and started again, a call from one of its own tasks does nothing.
	// running() tells whether FreeRTOS created the thread.
	Scheduler& start();

	// Deletes the thread between two runs of the due tasks; called from one of its own tasks, deletes it at once and never returns.
	void stop();

private:
	void loop();

	Thread thread;

	// held while the thread runs the due tasks, so stop() never deletes it mid-run
	w2b::FreeRtosMutex threadLocked;
};

}  // namespace w2b
