#include <cstdio>

// Gives `pio run -e native` something to build; the unit tests in test/ run with `pio test -e native`.
int main()
{
	std::puts("ble-relay host build " FW_VERSION);

	return 0;
}
