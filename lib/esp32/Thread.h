#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace w2b
{

// A FreeRTOS task pinned to a core, running a callable once, for blocking work (a queue, a socket, a serial port). The object must
// outlive the running thread; keep it global. Firmware only.
class Thread
{
public:
	// how start() creates the task
	struct Settings
	{
		// the task's name, shown by FreeRTOS diagnostics
		const char* name;

		// stack size in bytes
		uint32_t stackBytes;

		// FreeRTOS priority
		UBaseType_t priority;

		// 0 or 1, or tskNO_AFFINITY
		BaseType_t core;
	};

	// the callable the thread runs
	using Body = std::function<void()>;

	// settings: copied; nothing runs before start()
	explicit Thread(const Settings& settings);

	~Thread();

	Thread(const Thread&) = delete;

	Thread& operator=(const Thread&) = delete;

	// true from start() until the body returns or stop()
	inline bool running() const { return alive; }

	// the core the task runs on, as Settings gave it
	inline BaseType_t core() const { return settings.core; }

	// maximum avaliable stack bytes
	inline const uint32_t& stackBytes() const { return settings.stackBytes; }

	// false while a previous body is still running or when FreeRTOS refuses; the thread ends when the body returns
	bool start(Body body);

	// force: deletes the task wherever it is, so a mutex it holds stays taken; called from the thread's own body it never returns
	void stop();

	// true when called from this thread's own body
	bool isCurrent() const;

	// the most stack the task has used since it started, in bytes; 0 while it does not run
	size_t maxStackUsed() const;

private:
	static void entry(void* arg);

	void run();

	Settings settings;

	Body work;

	std::atomic<bool> alive{false};

	// the running task; null before it starts and after it ends
	std::atomic<TaskHandle_t> handle{nullptr};
};

}  // namespace w2b
