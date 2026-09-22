#include "BleScanList.h"

#include <algorithm>

#include <ArduinoJson.h>

namespace w2b
{

void BleScanList::add(const BleDevice& report)
{
	auto same = std::find_if(found.begin(), found.end(),
		[&report](const BleDevice& known)
		{
			return known.address() == report.address();
		});

	BleDevice device = report;

	if (same != found.end())
	{
		device = *same;
		device.merge(report);

		// the same signal keeps the entry's place
		if (device.rssi() == same->rssi())
		{
			*same = device;
			return;
		}

		found.erase(same);
	}

	// After every entry at least as strong, so equal signals keep the order they were found in.
	auto weaker = std::find_if(found.begin(), found.end(),
		[&device](const BleDevice& known)
		{
			return known.rssi() < device.rssi();
		});

	found.insert(weaker, device);
}

void BleScanList::clear()
{
	found.clear();
}

void BleScanList::toJson(JsonObject& json) const
{
	JsonArray list = json["devices"].to<JsonArray>();

	for (const BleDevice& device : found)
	{
		JsonObject item = list.add<JsonObject>();
		device.toJson(item);
	}
}

}  // namespace w2b
