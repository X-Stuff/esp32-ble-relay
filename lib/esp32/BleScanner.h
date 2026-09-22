#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <NimBLEScan.h>

#include "BleDeviceBase.h"
#include "BleScanList.h"
#include "Component.h"
#include "ConfigData.h"
#include "FreeRtosMutex.h"
#include "TargetConfig.h"

namespace w2b
{

// Runs NimBLE's scan for the page's scan list and for the device searches of the BLE connections. Also shares the radio: NimBLE
// cannot scan and connect at the same time. NimBLE's callbacks only store data; update() on the relay thread does the work.
class BleScanner : public Component, private NimBLEScanCallbacks
{
public:
	// page scan length when the request gives none
	static constexpr uint32_t kDefaultSeconds = 15;

	// the longest page scan
	static constexpr uint32_t kMaxSeconds = 30;

	// how long a search looks for its device; only the time while NimBLE scans counts
	static constexpr uint32_t kSearchTimeoutMs = 10000;

	// search by name: after the first device with the name, how long to keep looking for a stronger one with the same name
	static constexpr uint32_t kNameWaitMs = 1000;

	// searches at the same time: one per BLE connection
	static constexpr size_t kMaxSearches = ConfigData::kMaxActiveRules;

	// State of the page scan
	enum class State : uint8_t
	{
		// no scan since boot, or its devices cleared
		Idle,

		// requested; update() starts it when the radio is free
		Requested,

		// NimBLE scans; onScanEnd() sets Ended
		Running,

		// NimBLE stopped the scan; update() sets Done or Failed. Shown as running until then
		Ended,

		// the scan ran for its full time; the devices are in the list
		Done,

		// the scan did not start, the radio stayed busy for BleConnection::kRadioWaitMs, or NimBLE stopped it early
		Failed,
	};

	// State of a device search
	enum class SearchState : uint8_t
	{
		// slot not used
		Free,

		// looking for the device
		Searching,

		// the device was found
		Found,

		// the device was not found within kSearchTimeoutMs
		NotFound,
	};

	// "idle", "requested", "running" (Ended too), "done", "failed"
	static const char* stateStr(State state);

	// Sets up the scan: active, half of the air time, every report to the callbacks, none stored by NimBLE. After BleService.
	void setup() override;

	// Page scan: starts it when Requested, reads its result when Ended. Searches: updates their timings and runs NimBLE's scan for them.
	void update(uint32_t dtMs) override;

	// Requests a page scan of seconds, limited to 1 .. kMaxSeconds; a scan already requested or running is not restarted.
	// Returns the state after the request.
	State request(uint32_t seconds);

	// Copies the devices found so far by the page scan into devices; returns the page scan's state.
	State read(BleScanList& devices) const;

	// Frees the page scan's devices, which the page keeps itself, and goes back to Idle; false, nothing freed, while a scan is
	// requested or runs.
	bool clearDevices();

	// the length of the page scan requested or running, or of the last one
	uint32_t seconds() const;

	// the page scan's state
	State currentState() const;

	// Starts a search for target: by its address, or the strongest device with its name. Returns the slot, or kMaxSearches when all
	// slots are used.
	size_t startSearch(const TargetConfig& target);

	// Reads the search in slot: once Found, the device (address, address type, RSSI) in device, and in duplicateName whether another
	// device with the same name was seen too.
	SearchState readSearch(size_t slot, std::optional<BleDeviceBase>& device, bool& duplicateName) const;

	// Stops the search in slot and frees the slot, in any state.
	void stopSearch(size_t slot);

	// For holder's connect or page scan, since NimBLE does one at a time. True when the radio is free or holder's already; the search
	// scan stops then and waits, like everyone else, until holder releases it.
	bool acquireRadio(const void* holder);

	// Frees the radio when holder has it, nothing otherwise, so a holder may release on every way out.
	void releaseRadio(const void* holder);

private:
	// A search for one device, for a BLE connection
	struct Search
	{
		SearchState state = SearchState::Free;

		std::optional<TargetConfig> target;

		// the strongest matching device so far
		std::optional<BleDeviceBase> device;

		// another device with the same name was seen too
		bool duplicateName = false;

		// scan time spent on this search
		uint32_t searchedMs = 0;

		// scan time since the first device with the name was found
		uint32_t sinceFirstMatchMs = 0;
	};

	// NimBLE host task: an advertisement, with its scan response if one came; added to the page's list while Running, and checked
	// against the searches
	void onResult(const NimBLEAdvertisedDevice* advertised) override;

	// NimBLE host task: while Running, sets Ended and stores the reason (0: the scan ran its full time); else the search scan ended
	void onScanEnd(const NimBLEScanResults& results, int reason) override;

	// NimBLE host task, under the lock: checks report against every running search
	void matchSearches(const BleDevice& report);

	// Requested -> Running once it has the radio; Failed after BleConnection::kRadioWaitMs without it, or when NimBLE does not start
	// the scan
	void startScan(uint32_t dtMs);

	// Ended -> Done when the scan ran its full time, else Failed; releases the radio and logs how it ended
	void finishScan();

	// updates the searches' timings, and starts or stops the search scan
	void updateSearches(uint32_t dtMs);

	// under the lock: NotFound after kSearchTimeoutMs; by name Found kNameWaitMs after the first match
	void updateTimings(Search& search, uint32_t dtMs);

	// under the lock: a page scan is requested or running
	bool isPageScanActive() const;

	void setState(State next);

private:
	// protects everything below; used by the relay thread, the probe on the application thread, the web server and NimBLE's host task
	mutable FreeRtosMutex lock;

	// request() changes it from Idle, Done and Failed, update() from Requested and Ended, onScanEnd() from Running
	State state = State::Idle;

	// written by request() in Idle, Done and Failed only
	uint32_t requestedSeconds = kDefaultSeconds;

	// how long the requested page scan has waited for the radio; set by request(), counted by startScan() while Requested
	uint32_t radioWaitedMs = 0;

	// NimBLE's reason for the end of the last page scan; written by onScanEnd() when it sets Ended
	int endReason = 0;

	// cleared by startScan(), filled by onResult() while Running, freed by clearDevices()
	BleScanList scanList;

	std::array<Search, kMaxSearches> searches{};

	// NimBLE scans for the searches, not for the page
	bool searchScanRunning = false;

	// who has the radio: a connection, the probe or the page scan; null when it is free
	const void* radioHolder = nullptr;
};

}  // namespace w2b
