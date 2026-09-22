#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Component.h"
#include "JsonForward.h"

struct timeval;

namespace w2b
{

class TimeZone;

// The wall clock: SNTP against two servers, which lwIP repeats every 3 h, and the local time of the configured zone. Sets the system
// clock and TZ; sends NTP requests (UDP 123) and DNS queries for the servers on its own. Firmware only.
class TimeSync : public Component
{
public:
	// server1, server2: NTP server names; lwIP keeps the pointers, so they live for the whole run (string literals)
	TimeSync(const char* server1, const char* server2);

	// Starts SNTP; after the WiFi start, which brings up the network stack.
	void setup() override;

	// logs each sync
	void update(uint32_t dtMs) override;

	// Sets TZ to the zone's rule and logs it; before the schedulers start, then on the application thread only.
	void applyTimeZone(const TimeZone& timeZone);

	// Logger::Stamp: the local date and time once the clock is set, the uptime before; any task
	static size_t logStamp(char* out, size_t cap);

	// /api/status "time": local ("25/12/2026 15:38:42", empty while the clock is not set), timezone, synced (NTP answered in this run),
	// last_sync_s (null before the first answer)
	void toJson(JsonObject& json) const;

private:
	static void onSync(timeval* tv);

	const char* server1;

	const char* server2;

	// the zone in use, a table entry, so the web server's task reads it without a lock
	std::atomic<const TimeZone*> zone{nullptr};

	// application thread only: syncs logged so far
	uint32_t logged = 0;
};

}  // namespace w2b
