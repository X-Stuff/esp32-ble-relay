#include <unity.h>

#include "DigitalOut.h"
#include "LedIndicator.h"

using w2b::LedIndicator;
using w2b::LedPattern;

namespace
{

// Records what the LED wrote to its pin.
struct FakePin : w2b::DigitalOut
{
	bool ready = false;

	bool high = false;

	int writes = 0;

	void setup() override
	{
		ready = true;
	}

	void write(bool level) override
	{
		high = level;
		++writes;
	}
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

static void test_setup_configures_pin_and_writes_off()
{
	FakePin pin;
	LedIndicator led(pin, false);

	led.setup();

	TEST_ASSERT_TRUE(pin.ready);
	TEST_ASSERT_FALSE(pin.high);
	TEST_ASSERT_EQUAL_INT(1, pin.writes);
	TEST_ASSERT_FALSE(led.isOn());
}

static void test_active_low_inverts_pin_level()
{
	FakePin pin;
	LedIndicator led(pin, true);

	led.set(LedPattern::Solid);
	led.setup();
	TEST_ASSERT_TRUE(led.isOn());
	TEST_ASSERT_FALSE(pin.high);  // on = LOW

	led.set(LedPattern::Off);
	led.update(10);
	TEST_ASSERT_FALSE(led.isOn());
	TEST_ASSERT_TRUE(pin.high);
}

static void test_off_and_solid_are_constant()
{
	FakePin pin;
	LedIndicator led(pin, false);
	led.setup();

	led.update(123456);
	TEST_ASSERT_FALSE(pin.high);

	led.set(LedPattern::Solid);
	led.update(1);
	TEST_ASSERT_TRUE(pin.high);
	led.update(999999);
	TEST_ASSERT_TRUE(pin.high);
}

static void test_blink_1hz_half_second_on_half_off()
{
	FakePin pin;
	LedIndicator led(pin, false);
	led.set(LedPattern::Blink1Hz);
	led.setup();

	TEST_ASSERT_TRUE(pin.high);   // phase 0
	led.update(499);
	TEST_ASSERT_TRUE(pin.high);   // 499
	led.update(1);
	TEST_ASSERT_FALSE(pin.high);  // 500
	led.update(499);
	TEST_ASSERT_FALSE(pin.high);  // 999
	led.update(1);
	TEST_ASSERT_TRUE(pin.high);   // 1000 wraps to 0
}

static void test_blink_5hz()
{
	FakePin pin;
	LedIndicator led(pin, false);
	led.set(LedPattern::Blink5Hz);
	led.setup();

	TEST_ASSERT_TRUE(pin.high);
	led.update(100);
	TEST_ASSERT_FALSE(pin.high);
	led.update(100);
	TEST_ASSERT_TRUE(pin.high);
}

static void test_heartbeat_short_blink_every_3s()
{
	FakePin pin;
	LedIndicator led(pin, false);
	led.set(LedPattern::Heartbeat3s);
	led.setup();

	TEST_ASSERT_TRUE(pin.high);
	led.update(49);
	TEST_ASSERT_TRUE(pin.high);
	led.update(1);
	TEST_ASSERT_FALSE(pin.high);  // 50
	led.update(2949);
	TEST_ASSERT_FALSE(pin.high);  // 2999
	led.update(1);
	TEST_ASSERT_TRUE(pin.high);   // 3000 wraps to 0
}

static void test_double_blink()
{
	FakePin pin;
	LedIndicator led(pin, false);
	led.set(LedPattern::DoubleBlink);
	led.setup();

	TEST_ASSERT_TRUE(pin.high);   // 0
	led.update(100);
	TEST_ASSERT_FALSE(pin.high);  // 100
	led.update(100);
	TEST_ASSERT_TRUE(pin.high);   // 200
	led.update(100);
	TEST_ASSERT_FALSE(pin.high);  // 300
	led.update(1699);
	TEST_ASSERT_FALSE(pin.high);  // 1999
	led.update(1);
	TEST_ASSERT_TRUE(pin.high);   // 2000 wraps to 0
}

static void test_large_dt_wraps_phase()
{
	FakePin pin;
	LedIndicator led(pin, false);
	led.set(LedPattern::Blink1Hz);
	led.setup();

	led.update(1700);
	TEST_ASSERT_FALSE(pin.high);  // 700
	led.update(2800);
	TEST_ASSERT_FALSE(pin.high);  // 3500 -> 500
	led.update(500);
	TEST_ASSERT_TRUE(pin.high);   // 1000 -> 0
}

static void test_switching_pattern_restarts_phase()
{
	FakePin pin;
	LedIndicator led(pin, false);
	led.set(LedPattern::Blink1Hz);
	led.setup();

	led.update(700);
	TEST_ASSERT_FALSE(pin.high);

	led.set(LedPattern::Blink5Hz);
	led.update(0);
	TEST_ASSERT_TRUE(pin.high);   // phase 0 of the new pattern
	led.update(99);
	TEST_ASSERT_TRUE(pin.high);
	led.update(1);
	TEST_ASSERT_FALSE(pin.high);
	TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::Blink5Hz), static_cast<int>(led.pattern()));
}

static void test_setting_same_pattern_keeps_phase()
{
	FakePin pin;
	LedIndicator led(pin, false);
	led.set(LedPattern::Blink1Hz);
	led.setup();

	led.update(700);
	led.set(LedPattern::Blink1Hz);  // no-op
	led.update(0);

	TEST_ASSERT_FALSE(pin.high);  // still at 700
}

static void test_flicker_inverts_until_50ms_elapsed()
{
	FakePin pin;
	LedIndicator led(pin, false);
	led.set(LedPattern::Solid);
	led.setup();
	TEST_ASSERT_TRUE(pin.high);

	led.flicker();
	led.update(10);
	TEST_ASSERT_FALSE(pin.high);  // 10 ms in
	led.update(39);
	TEST_ASSERT_FALSE(pin.high);  // 49 ms in
	led.update(1);
	TEST_ASSERT_TRUE(pin.high);   // 50 ms: back to the pattern

	led.set(LedPattern::Off);
	led.update(10);
	TEST_ASSERT_FALSE(pin.high);
	led.flicker();
	led.update(10);
	TEST_ASSERT_TRUE(pin.high);   // Off inverted = on
	led.update(40);
	TEST_ASSERT_FALSE(pin.high);
}

static void test_is_on_matches_pin_for_active_high()
{
	FakePin pin;
	LedIndicator led(pin, false);
	led.set(LedPattern::DoubleBlink);
	led.setup();

	for (int i = 0; i < 40; ++i)
	{
		led.update(53);
		TEST_ASSERT_EQUAL(led.isOn(), pin.high);
	}
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_setup_configures_pin_and_writes_off);
	RUN_TEST(test_active_low_inverts_pin_level);
	RUN_TEST(test_off_and_solid_are_constant);
	RUN_TEST(test_blink_1hz_half_second_on_half_off);
	RUN_TEST(test_blink_5hz);
	RUN_TEST(test_heartbeat_short_blink_every_3s);
	RUN_TEST(test_double_blink);
	RUN_TEST(test_large_dt_wraps_phase);
	RUN_TEST(test_switching_pattern_restarts_phase);
	RUN_TEST(test_setting_same_pattern_keeps_phase);
	RUN_TEST(test_flicker_inverts_until_50ms_elapsed);
	RUN_TEST(test_is_on_matches_pin_for_active_high);

	return UNITY_END();
}
