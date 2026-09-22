#include "TaskList.h"

#include <algorithm>

namespace w2b
{

TaskList::TaskList(Clock clock)
	: clock(clock)
{
}

// An empty callable would abort the firmware when run (no exceptions): both entry points drop it.
TaskList& TaskList::every(uint32_t periodMs, Task task)
{
	if (task)
	{
		add(periodMs, true, std::move(task));
	}

	return *this;
}

TaskList& TaskList::once(uint32_t delayMs, Once task)
{
	if (task)
	{
		add(delayMs, false, [task](uint32_t)
			{
				task();
			});
	}

	return *this;
}

uint32_t TaskList::runOnce()
{
	merge();
	runDue();
	merge();
	dropFinished();

	return nextDueIn();
}

void TaskList::add(uint32_t delayMs, bool repeat, Task task)
{
	auto entry = Entry
	{
		.lastMs = clock(),
		.delayMs = delayMs,
		.repeat = repeat,
		.task = std::move(task)
	};
	pending.emplace_back(std::move(entry));
}

void TaskList::merge()
{
	for (Entry& entry : pending)
	{
		entries.push_back(std::move(entry));
	}

	pending.clear();
}

void TaskList::runDue()
{
	for (Entry& entry : entries)
	{
		// Durations, not timestamps: the unsigned difference is right across the clock wrap.
		const uint32_t elapsedMs = clock() - entry.lastMs;

		// Defensive: stored tasks are callable, but an empty one is never called.
		if (entry.task == nullptr || elapsedMs < entry.delayMs)
		{
			continue;
		}

		// lastMs is taken before the call, so the task's own run time does not stretch the period.
		entry.lastMs = clock();
		entry.task(elapsedMs);

		if (!entry.repeat)
		{
			entry.task = nullptr;
		}
	}
}

void TaskList::dropFinished()
{
	auto finished = std::remove_if(entries.begin(), entries.end(),
		[](const Entry& entry)
		{
			return !entry.task;
		});

	entries.erase(finished, entries.end());
}

uint32_t TaskList::nextDueIn() const
{
	const uint32_t nowMs = clock();
	uint32_t next = kMaxSleepMs;

	for (const Entry& entry : entries)
	{
		if (entry.task)
		{
			const uint32_t elapsedMs = nowMs - entry.lastMs;

			if (elapsedMs > entry.delayMs)
			{
				return 0;
			}

			const uint32_t remaining = entry.delayMs - elapsedMs;
			next = remaining < next ? remaining : next;
		}
	}

	return next;
}

}  // namespace w2b
