#include "TimeZones.h"

#include <algorithm>
#include <cstring>

namespace w2b
{

// defined in TimeZoneData.cpp
extern const TimeZone kTimeZones[];
extern const size_t kTimeZoneCount;

// Binary search: the generator writes the table sorted by name.
const TimeZone* findTimeZone(const std::string& name)
{
	const TimeZone* end = kTimeZones + kTimeZoneCount;
	const TimeZone* found = std::lower_bound(kTimeZones, end, name,
		[](const TimeZone& zone, const std::string& key)
		{
			return std::strcmp(zone.name(), key.c_str()) < 0;
		});

	if (found == end || name != found->name())
	{
		return nullptr;
	}

	return found;
}

size_t timeZoneCount()
{
	return kTimeZoneCount;
}

const TimeZone& timeZoneAt(size_t index)
{
	return kTimeZones[index];
}

}  // namespace w2b
