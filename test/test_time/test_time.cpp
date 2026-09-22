#include <unity.h>

#include <cstring>
#include <string>

#include "TimeZones.h"

using w2b::TimeZone;

void setUp()
{
}

void tearDown()
{
}

static void test_known_zones_carry_their_tzdata_rules()
{
	const TimeZone* berlin = w2b::findTimeZone("Europe/Berlin");
	TEST_ASSERT_NOT_NULL(berlin);
	TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", berlin->posix());

	const TimeZone* kolkata = w2b::findTimeZone("Asia/Kolkata");
	TEST_ASSERT_NOT_NULL(kolkata);
	TEST_ASSERT_EQUAL_STRING("IST-5:30", kolkata->posix());

	const TimeZone* newYork = w2b::findTimeZone("America/New_York");
	TEST_ASSERT_NOT_NULL(newYork);
	TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", newYork->posix());
}

// zone.tab files Cyprus under Asia; the table lists it under its tzdata link in Europe.
static void test_cyprus_is_listed_in_europe()
{
	const TimeZone* nicosia = w2b::findTimeZone("Europe/Nicosia");
	TEST_ASSERT_NOT_NULL(nicosia);
	TEST_ASSERT_EQUAL_STRING("EET-2EEST,M3.5.0/3,M10.5.0/4", nicosia->posix());
	TEST_ASSERT_NULL(w2b::findTimeZone("Asia/Nicosia"));
}

static void test_the_default_zone_is_in_the_table()
{
	const TimeZone* utc = w2b::findTimeZone(w2b::kDefaultTimeZone);
	TEST_ASSERT_NOT_NULL(utc);
	TEST_ASSERT_EQUAL_STRING("UTC0", utc->posix());
}

static void test_unknown_names_are_not_found()
{
	TEST_ASSERT_NULL(w2b::findTimeZone(""));
	TEST_ASSERT_NULL(w2b::findTimeZone("Mars/Olympus"));
	TEST_ASSERT_NULL(w2b::findTimeZone("europe/berlin"));
	TEST_ASSERT_NULL(w2b::findTimeZone("Europe/Berlin "));
	TEST_ASSERT_NULL(w2b::findTimeZone("Europe"));
}

// The lookup is a binary search, so the table must be sorted, and every entry must have a rule.
static void test_table_is_sorted_unique_and_complete()
{
	TEST_ASSERT_TRUE(w2b::timeZoneCount() > 300);

	for (size_t i = 0; i < w2b::timeZoneCount(); ++i)
	{
		const TimeZone& zone = w2b::timeZoneAt(i);

		TEST_ASSERT_TRUE(std::strlen(zone.name()) > 0);
		TEST_ASSERT_TRUE(std::strlen(zone.posix()) > 0);
		TEST_ASSERT_EQUAL_PTR(&zone, w2b::findTimeZone(zone.name()));

		if (i > 0)
		{
			TEST_ASSERT_TRUE_MESSAGE(std::strcmp(w2b::timeZoneAt(i - 1).name(), zone.name()) < 0, zone.name());
		}
	}
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_known_zones_carry_their_tzdata_rules);
	RUN_TEST(test_cyprus_is_listed_in_europe);
	RUN_TEST(test_the_default_zone_is_in_the_table);
	RUN_TEST(test_unknown_names_are_not_found);
	RUN_TEST(test_table_is_sorted_unique_and_complete);

	return UNITY_END();
}
