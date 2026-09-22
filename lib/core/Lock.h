#pragma once

namespace w2b
{

// Mutual exclusion for modules shared between tasks. The firmware implements it over a FreeRTOS mutex (lib/esp32/FreeRtosMutex);
// tests use a fake.
class Lock
{
public:
	virtual ~Lock() = default;

	// blocks until the calling task holds the lock
	virtual void lock() = 0;

	// releases the lock
	virtual void unlock() = 0;
};

// Holds a Lock for the current scope. A null lock means single-task use.
class LockGuard
{
public:
	// locks lock unless it is null; the destructor unlocks it
	explicit LockGuard(Lock* lock)
		: held(lock)
	{
		if (held != nullptr)
		{
			held->lock();
		}
	}

	~LockGuard()
	{
		if (held != nullptr)
		{
			held->unlock();
		}
	}

	LockGuard(const LockGuard&) = delete;

	LockGuard& operator=(const LockGuard&) = delete;

private:
	Lock* held;
};

}  // namespace w2b
