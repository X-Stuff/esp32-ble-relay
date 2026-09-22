#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "BleAddress.h"
#include "BleConnection.h"
#include "BleDeviceBase.h"
#include "Component.h"
#include "ConfigData.h"
#include "DeviceRules.h"
#include "FreeRtosMutex.h"
#include "RelayStatus.h"

namespace w2b
{

class BleScanner;
class Config;

// Manages the BLE connections of the enabled rules: one BleConnection per device, shared by its rules. Applies every configuration
// change: a rule disabled or changed closes its connection at once.
class BleConnectionManager : public Component
{
public:
	// how often the rules' statuses are made again, and at every configuration change
	static constexpr uint32_t kStatusPeriodMs = 250;

	// config: the rules, read again when currentRevision() changes; scanner: device searches and the radio
	BleConnectionManager(const Config& config, BleScanner& scanner);

	// the relay's own address, as NimBLE reported it to setup(); empty while NimBLE does not run
	inline const std::optional<BleAddress>& ownAddress() const { return own; }

	// the type of ownAddress()
	inline BleDeviceBase::AddressType ownAddressType() const { return ownType; }

	// Takes the relay's own address from NimBLE, which BleService started and set up, and initializes the connections.
	void setup() override;

	// Applies configuration changes, updates every connection, and stores the rules' statuses for readStatuses().
	void update(uint32_t dtMs) override;

	// Runs reader on the state of every rule, in the configuration's order and at most kStatusPeriodMs old, under the lock; reader
	// stays short, the relay thread waits for it to store the next states.
	void readStatuses(const std::function<void(const std::vector<RelayStatus>& statuses)>& reader) const;

private:
	// on a new revision: closes the connections of devices removed or changed, new devices wait for a free connection; true then
	bool applyConfig();

	// gives the waiting devices to free connections, once no removed device's connection still closes
	void assignWaitingDevices();

	void updateStatuses();

private:
	const Config& config;

	BleScanner& scanner;

	// setup() found NimBLE running
	bool nimBleRunning = false;

	std::optional<BleAddress> own;

	BleDeviceBase::AddressType ownType = BleDeviceBase::AddressType::Public;

	uint32_t appliedRevision = 0;

	uint32_t sinceStatusMs = 0;

	// the configuration's rules, for the status of every rule
	ConfigData::Rules rules;

	// devices of enabled rules without a connection yet: no connection is free, or a removed device's still closes
	std::vector<DeviceRules> waitingDevices;

	std::array<BleConnection, ConfigData::kMaxActiveRules> connections;

	// the states updateStatuses() makes before it swaps them in; keeps its storage between updates
	std::vector<RelayStatus> nextStatuses;

	// protects lastStatuses; used by the relay thread and the web server
	mutable FreeRtosMutex lock;

	std::vector<RelayStatus> lastStatuses;
};

}  // namespace w2b
