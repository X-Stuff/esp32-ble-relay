#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace w2b
{

// The scheduling logic behind Scheduler: tasks run every period or once after a delay; runOnce() returns how long to sleep. Tasks
// may schedule further tasks from inside their own run; scheduling from another thread is not supported.
class TaskList
{
public:
	// Longest sleep runOnce() asks for, so an idle list still wakes up now and then.
	static constexpr uint32_t kMaxSleepMs = 100;

	// milliseconds from any fixed point; may wrap
	using Clock = uint32_t (*)();

	// a repeated task; dtMs: the milliseconds since its previous run, or since it was scheduled on the first run
	using Task = std::function<void(uint32_t dtMs)>;

	// a one-shot task
	using Once = std::function<void()>;

	// clock: the time source for every due check
	explicit TaskList(Clock clock);

	// the tasks scheduled; a one-shot counts until it has run
	inline size_t size() const { return entries.size() + pending.size(); }

	// Repeats task with periodMs as the minimum interval, first after periodMs; period 0 runs on every runOnce(). An empty callable is
	// ignored.
	TaskList& every(uint32_t periodMs, Task task);

	// runs task once, delayMs after this call; an empty callable is ignored
	TaskList& once(uint32_t delayMs, Once task);

	// Runs the due tasks in scheduling order; returns the milliseconds until the next one is due, at most kMaxSleepMs.
	uint32_t runOnce();

private:
	// one scheduled task
	struct Entry
	{
		// when scheduled, then when last run
		uint32_t lastMs;

		// due when clock() - lastMs reaches this: the initial delay, then the period
		uint32_t delayMs;

		bool repeat;

		// empty once a one-shot has run
		Task task;
	};

	void add(uint32_t delayMs, bool repeat, Task task);

	// moves pending into entries
	void merge();

	void runDue();

	void dropFinished();

	uint32_t nextDueIn() const;

	Clock clock;

	std::vector<Entry> entries;

	// scheduled while runDue() iterates entries; merged afterwards
	std::vector<Entry> pending;
};

}  // namespace w2b
