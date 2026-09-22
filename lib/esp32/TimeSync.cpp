#include "TimeSync.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_netif_sntp.h>
#include <esp_timer.h>

#include "LogStamp.h"
#include "Logger.h"
#include "TimeZones.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "time";

// A clock before 2025 was never set: a power cycle starts it at 1970.
constexpr time_t kClockSetAfter = 1735689600;

// Written by onSync() on lwIP's task, read everywhere.
std::atomic<uint32_t> syncCount{0};
std::atomic<uint32_t> lastSyncUptimeS{0};

uint32_t uptimeS()
{
	return static_cast<uint32_t>(esp_timer_get_time() / 1000000);
}

// The local time now; false while the clock is not set.
bool localNow(std::tm& local, uint32_t& millis)
{
	timeval now;
	gettimeofday(&now, nullptr);

	if (now.tv_sec < kClockSetAfter)
	{
		return false;
	}

	localtime_r(&now.tv_sec, &local);
	millis = static_cast<uint32_t>(now.tv_usec / 1000);

	return true;
}

}  // namespace

TimeSync::TimeSync(const char* server1, const char* server2)
	: server1(server1), server2(server2)
{
}

// Nothing waits for the clock: the log stamps switch over by themselves.
void TimeSync::setup()
{
	esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST(server1, server2));
	sntp.wait_for_sync = false;
	sntp.sync_cb = onSync;

	const esp_err_t started = esp_netif_sntp_init(&sntp);

	if (started != ESP_OK)
	{
		Logger::error(kTag, "SNTP did not start (%s); the log keeps the uptime", esp_err_to_name(started));
		return;
	}

	Logger::info(kTag, "SNTP started: %s, %s; the log shows the local time once the clock is set", server1, server2);
}

void TimeSync::update(uint32_t /*dtMs*/)
{
	const uint32_t syncs = syncCount;

	if (syncs == logged)
	{
		return;
	}

	if (logged == 0)
	{
		Logger::info(kTag, "clock set by NTP at uptime %lu ms; lines before carry the uptime", (unsigned long)millis());
	}
	else
	{
		Logger::debug(kTag, "clock synced by NTP (%u syncs)", (unsigned)syncs);
	}

	logged = syncs;
}

void TimeSync::applyTimeZone(const TimeZone& timeZone)
{
	setenv("TZ", timeZone.posix(), 1);
	tzset();
	zone = &timeZone;

	Logger::info(kTag, "time zone %s (%s)", timeZone.name(), timeZone.posix());
}

size_t TimeSync::logStamp(char* out, size_t cap)
{
	std::tm local{};
	uint32_t ms = 0;

	if (!localNow(local, ms))
	{
		return formatUptimeStamp(millis(), out, cap);
	}

	return formatWallStamp(local, ms, out, cap);
}

void TimeSync::toJson(JsonObject& json) const
{
	std::tm local{};
	uint32_t ms = 0;
	char text[24] = "";

	if (localNow(local, ms))
	{
		snprintf(text, sizeof(text), "%02d/%02d/%04d %02d:%02d:%02d", local.tm_mday, local.tm_mon + 1, local.tm_year + 1900, local.tm_hour,
			local.tm_min, local.tm_sec);
	}

	const TimeZone* current = zone;

	json["local"] = text;
	json["timezone"] = current != nullptr ? current->name() : kDefaultTimeZone;
	json["synced"] = syncCount > 0;

	if (syncCount > 0)
	{
		json["last_sync_s"] = uptimeS() - lastSyncUptimeS;
	}
	else
	{
		json["last_sync_s"] = nullptr;
	}
}

// lwIP's task, after settimeofday(): count only.
void TimeSync::onSync(timeval* /*tv*/)
{
	lastSyncUptimeS = uptimeS();
	++syncCount;
}

}  // namespace w2b
