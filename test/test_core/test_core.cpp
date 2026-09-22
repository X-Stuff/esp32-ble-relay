#include <unity.h>

#include "FixedString.h"

using w2b::FixedString;

void setUp()
{
}

void tearDown()
{
}

static void test_fixed_string_holds_text_up_to_its_capacity()
{
	FixedString<5> text("abc");

	TEST_ASSERT_EQUAL_STRING("abc", text.c_str());
	TEST_ASSERT_EQUAL(3, text.size());
	TEST_ASSERT_TRUE(text.fits());
	TEST_ASSERT_TRUE(text == "abc");
	TEST_ASSERT_FALSE(text == "abcd");

	text.assign("abcde");
	TEST_ASSERT_EQUAL_STRING("abcde", text.c_str());
	TEST_ASSERT_TRUE(text.fits());

	text.assign(nullptr);
	TEST_ASSERT_TRUE(text.empty());
	TEST_ASSERT_TRUE(text.fits());
}

static void test_fixed_string_cuts_a_longer_text_and_says_so()
{
	FixedString<5> text("abcdefgh");

	TEST_ASSERT_EQUAL_STRING("abcde", text.c_str());
	TEST_ASSERT_EQUAL(5, text.size());
	TEST_ASSERT_FALSE(text.fits());
	TEST_ASSERT_TRUE(text != FixedString<5>("abcde"));

	text.assign("ok");
	TEST_ASSERT_TRUE(text.fits());
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_fixed_string_holds_text_up_to_its_capacity);
	RUN_TEST(test_fixed_string_cuts_a_longer_text_and_says_so);

	return UNITY_END();
}
