// The IANA time zones and their POSIX TZ rules. The table, TimeZoneData.cpp, holds tzdata's zones.
#pragma once

#include <cstddef>
#include <string>

namespace w2b
{

// A zone of the table: its IANA name and its POSIX TZ rule
class TimeZone
{
public:
	// name: IANA, "Europe/Berlin"; posix: the rule, "CET-1CEST,M3.5.0,M10.5.0/3"
	constexpr TimeZone(const char* name, const char* posix)
		: zoneName(name), rule(posix)
	{
	}

	// IANA name, "Europe/Berlin"
	inline const char* name() const { return zoneName; }

	// POSIX TZ string, "CET-1CEST,M3.5.0,M10.5.0/3"
	inline const char* posix() const { return rule; }

private:
	const char* zoneName;

	const char* rule;
};

// the name of the zone a config without "timezone" has
constexpr const char* kDefaultTimeZone = "UTC";

// The zone named name; null for a name the table does not hold.
const TimeZone* findTimeZone(const std::string& name);

// the zone of kDefaultTimeZone, which the table always holds
inline const TimeZone& defaultTimeZone() { return *findTimeZone(kDefaultTimeZone); }

// zones in the table
size_t timeZoneCount();

// Zone number index of the table, sorted by name; index below timeZoneCount().
const TimeZone& timeZoneAt(size_t index);

}  // namespace w2b
