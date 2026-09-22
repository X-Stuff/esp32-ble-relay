#include <unity.h>

#include <vector>

#include "TaskList.h"

using w2b::TaskList;

namespace
{

uint32_t g_now = 0;

uint32_t fakeClock()
{
	return g_now;
}

}  // namespace

void setUp()
{
	g_now = 0;
}

void tearDown()
{
}

static void test_every_runs_after_period_and_reports_dt()
{
	TaskList list(fakeClock);
	int calls = 0;
	uint32_t lastDt = 0;

	list.every(50, [&](uint32_t dtMs)
		{
			++calls;
			lastDt = dtMs;
		});

	TEST_ASSERT_EQUAL_UINT32(50, list.runOnce());  // nothing due yet: sleep 50
	TEST_ASSERT_EQUAL_INT(0, calls);

	g_now = 30;
	TEST_ASSERT_EQUAL_UINT32(20, list.runOnce());
	TEST_ASSERT_EQUAL_INT(0, calls);

	g_now = 50;
	TEST_ASSERT_EQUAL_UINT32(50, list.runOnce());  // ran, next in 50
	TEST_ASSERT_EQUAL_INT(1, calls);
	TEST_ASSERT_EQUAL_UINT32(50, lastDt);
}

static void test_sleep_is_the_smallest_remaining_delay_capped_at_max()
{
	TaskList list(fakeClock);

	TEST_ASSERT_EQUAL_UINT32(TaskList::kMaxSleepMs, list.runOnce());  // empty

	list.every(5000, [](uint32_t) {});
	TEST_ASSERT_EQUAL_UINT32(TaskList::kMaxSleepMs, list.runOnce());

	list.every(30, [](uint32_t) {});
	TEST_ASSERT_EQUAL_UINT32(30, list.runOnce());

	list.once(7, [] {});
	TEST_ASSERT_EQUAL_UINT32(7, list.runOnce());
}

static void test_late_wake_runs_with_real_dt_and_reschedules_from_now()
{
	TaskList list(fakeClock);
	uint32_t lastDt = 0;

	list.every(50, [&](uint32_t dtMs)
		{
			lastDt = dtMs;
		});

	g_now = 70;
	TEST_ASSERT_EQUAL_UINT32(50, list.runOnce());  // ran late; next due at 120
	TEST_ASSERT_EQUAL_UINT32(70, lastDt);

	g_now = 119;
	TEST_ASSERT_EQUAL_UINT32(1, list.runOnce());

	g_now = 120;
	list.runOnce();
	TEST_ASSERT_EQUAL_UINT32(50, lastDt);
}

static void test_once_runs_a_single_time_and_is_removed()
{
	TaskList list(fakeClock);
	int calls = 0;

	list.once(30, [&]
		{
			++calls;
		});

	TEST_ASSERT_EQUAL_UINT(1, list.size());

	g_now = 29;
	TEST_ASSERT_EQUAL_UINT32(1, list.runOnce());
	TEST_ASSERT_EQUAL_INT(0, calls);

	g_now = 30;
	TEST_ASSERT_EQUAL_UINT32(TaskList::kMaxSleepMs, list.runOnce());
	TEST_ASSERT_EQUAL_INT(1, calls);
	TEST_ASSERT_EQUAL_UINT(0, list.size());

	g_now = 1000;
	list.runOnce();
	TEST_ASSERT_EQUAL_INT(1, calls);
}

static void test_once_zero_runs_on_the_first_run()
{
	TaskList list(fakeClock);
	int calls = 0;

	list.once(0, [&]
		{
			++calls;
		});

	list.runOnce();

	TEST_ASSERT_EQUAL_INT(1, calls);
}

static void test_mixed_periods_share_one_list()
{
	TaskList list(fakeClock);
	int fastA = 0;
	int fastB = 0;
	int slowA = 0;
	int slowB = 0;

	list.every(10, [&](uint32_t)
		{
			++fastA;
		});

	list.every(10, [&](uint32_t)
		{
			++fastB;
		});

	list.every(50, [&](uint32_t)
		{
			++slowA;
		});

	list.every(50, [&](uint32_t)
		{
			++slowB;
		});

	// The owner's loop: run, then sleep exactly as long as asked, until 100 ms have passed.
	for (;;)
	{
		uint32_t sleepMs = list.runOnce();

		if (g_now >= 100)
		{
			break;
		}

		g_now += sleepMs;
	}

	TEST_ASSERT_EQUAL_INT(10, fastA);
	TEST_ASSERT_EQUAL_INT(10, fastB);
	TEST_ASSERT_EQUAL_INT(2, slowA);
	TEST_ASSERT_EQUAL_INT(2, slowB);
}

static void test_first_dt_counts_from_scheduling_time()
{
	TaskList list(fakeClock);
	uint32_t lastDt = 0;

	g_now = 100;
	list.every(10, [&](uint32_t dtMs)
		{
			lastDt = dtMs;
		});

	g_now = 125;
	list.runOnce();

	TEST_ASSERT_EQUAL_UINT32(25, lastDt);
}

static void test_a_task_may_schedule_another_task()
{
	TaskList list(fakeClock);
	int inner = 0;

	list.once(0, [&]
		{
			list.once(5, [&]
				{
					++inner;
				});
		});

	TEST_ASSERT_EQUAL_UINT32(5, list.runOnce());  // outer ran, inner is due in 5
	TEST_ASSERT_EQUAL_UINT(1, list.size());
	TEST_ASSERT_EQUAL_INT(0, inner);

	g_now = 5;
	list.runOnce();
	TEST_ASSERT_EQUAL_INT(1, inner);
	TEST_ASSERT_EQUAL_UINT(0, list.size());
}

static void test_tasks_run_in_scheduling_order()
{
	TaskList list(fakeClock);
	std::vector<int> order;

	list.once(0, [&]
		{
			order.push_back(1);
		});

	list.every(0, [&](uint32_t)
		{
			order.push_back(2);
		});

	list.once(0, [&]
		{
			order.push_back(3);
		});

	list.runOnce();

	TEST_ASSERT_EQUAL_UINT(3, order.size());
	TEST_ASSERT_EQUAL_INT(1, order[0]);
	TEST_ASSERT_EQUAL_INT(2, order[1]);
	TEST_ASSERT_EQUAL_INT(3, order[2]);
}

static void test_overdue_task_asks_for_no_sleep()
{
	TaskList list(fakeClock);

	list.every(0, [](uint32_t) {});
	TEST_ASSERT_EQUAL_UINT32(0, list.runOnce());  // due again right away

	TaskList other(fakeClock);
	int lateCalls = 0;

	other.every(25, [&](uint32_t)
		{
			++lateCalls;
		});

	// a slow task: 30 ms of work
	other.every(20, [](uint32_t)
		{
			g_now += 30;
		});

	g_now = 20;
	TEST_ASSERT_EQUAL_UINT32(0, other.runOnce());  // the 25 ms task became overdue meanwhile
	TEST_ASSERT_EQUAL_INT(0, lateCalls);

	other.runOnce();
	TEST_ASSERT_EQUAL_INT(1, lateCalls);
}

static void test_empty_callable_is_ignored()
{
	TaskList list(fakeClock);

	list.every(10, nullptr);
	list.once(0, TaskList::Once());

	TEST_ASSERT_EQUAL_UINT(0, list.size());
	TEST_ASSERT_EQUAL_UINT32(TaskList::kMaxSleepMs, list.runOnce());  // and nothing to call
}

static void test_survives_clock_wraparound()
{
	TaskList list(fakeClock);
	int calls = 0;

	g_now = 0xFFFFFFF0u;  // 16 ms before the wrap
	list.every(20, [&](uint32_t)
		{
			++calls;
		});

	g_now = 0xFFFFFFFFu;
	TEST_ASSERT_EQUAL_UINT32(5, list.runOnce());

	g_now = 4;  // 20 ms after scheduling
	list.runOnce();
	TEST_ASSERT_EQUAL_INT(1, calls);
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_every_runs_after_period_and_reports_dt);
	RUN_TEST(test_sleep_is_the_smallest_remaining_delay_capped_at_max);
	RUN_TEST(test_late_wake_runs_with_real_dt_and_reschedules_from_now);
	RUN_TEST(test_once_runs_a_single_time_and_is_removed);
	RUN_TEST(test_once_zero_runs_on_the_first_run);
	RUN_TEST(test_mixed_periods_share_one_list);
	RUN_TEST(test_first_dt_counts_from_scheduling_time);
	RUN_TEST(test_a_task_may_schedule_another_task);
	RUN_TEST(test_tasks_run_in_scheduling_order);
	RUN_TEST(test_overdue_task_asks_for_no_sleep);
	RUN_TEST(test_empty_callable_is_ignored);
	RUN_TEST(test_survives_clock_wraparound);

	return UNITY_END();
}
