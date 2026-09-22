#include <unity.h>
#include <cstring>

void setUp()
{
}

void tearDown()
{
}

static void test_fw_version_is_set()
{
	TEST_ASSERT_TRUE(std::strlen(FW_VERSION) > 0);
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_fw_version_is_set);

	return UNITY_END();
}
