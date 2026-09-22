#pragma once

#include <vector>

#include "BleDevice.h"
#include "JsonForward.h"

namespace w2b
{

// BLE scan results as the page lists them: one entry per address, strongest first, every device found. Not persisted: toJson only,
// for the HTTP API.
class BleScanList
{
public:
	// strongest first
	inline const std::vector<BleDevice>& devices() const { return found; }

	// A known address: merged into its entry, its address type kept. A new one: in signal order.
	void add(const BleDevice& report);

	// no devices
	void clear();

	// "devices": [...]
	void toJson(JsonObject& json) const;

private:
	std::vector<BleDevice> found;
};

}  // namespace w2b
