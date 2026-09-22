#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "BleAddress.h"
#include "BleDeviceBase.h"
#include "Component.h"
#include "FreeRtosMutex.h"
#include "GattTable.h"

struct ble_gap_event;
struct ble_gatt_error;
struct ble_gatt_svc;
struct ble_gatt_chr;
struct ble_gatt_attr;

namespace w2b
{

class BleScanner;

// The page's probe: connects to a device the scan found, reads the GATT table and the device's name, appearance and Device
// Information, writes nothing and disconnects. A state machine that update() runs, with NimBLE's C calls, none of which block.
class BleDiscovery : public Component
{
public:
	// State of the probe
	enum class State : uint8_t
	{
		// no probe since boot
		Idle,

		// requested; update() connects when the radio is free
		Requested,

		// waiting for the connect event
		Connecting,

		// discovering the primary services
		DiscoveringServices,

		// discovering one service's characteristics
		DiscoveringCharacteristics,

		// reading the values of GAP's and Device Information's characteristics
		ReadingValues,

		// waiting for the disconnect event; then Done or Failed
		Disconnecting,

		// the table read and the connection closed
		Done,

		// no connection, or the table not read; the log says why
		Failed,
	};

	// "idle", "requested", "connecting", "reading" (discovering, reading values, disconnecting), "done", "failed"
	static const char* stateStr(State state);

	// scanner: gives the radio for the connect. Set up after BleService, which starts NimBLE.
	explicit BleDiscovery(BleScanner& scanner);

	// Takes the relay's own address type from NimBLE, which BleService started and set up.
	void setup() override;

	// Runs the probe's state machine: acquires the radio, connects, discovers, reads the values and disconnects.
	void update(uint32_t dtMs) override;

	// Requests the probe of the device at address, of type as a scan reported it; false while one is requested or runs.
	bool request(const BleAddress& address, BleDeviceBase::AddressType type);

	// The state; the address requested last in address and, once Done, its GATT table in table.
	State read(BleAddress& address, GattTable& table) const;

	// the state
	State currentState() const;

private:
	// State of the last GATT request, set by its callback
	enum class RequestState : uint8_t
	{
		None,
		Running,
		Done,
		Failed,
	};

	// a primary service of the device
	struct Service
	{
		BleUuid uuid;

		uint16_t start = 0;

		uint16_t end = 0;
	};

	// a characteristic as NimBLE reports it
	struct FoundCharacteristic
	{
		BleUuid uuid;

		uint8_t properties = 0;

		uint16_t valueHandle = 0;
	};

	// The GATT request running: its state and what its callbacks received. The callbacks write it under the lock; startRequest()
	// resets it.
	struct GattRequest
	{
		RequestState state = RequestState::None;

		// NimBLE's error once Failed
		int error = 0;

		// service discovery: the primary services
		std::vector<Service> services;

		// characteristic discovery: one service's characteristics
		std::vector<FoundCharacteristic> characteristics;

		// value read: the value's first bytes
		GattValue::Bytes value{};

		size_t valueSize = 0;
	};

	// a characteristic whose value the probe reads
	struct ValueToRead
	{
		BleUuid uuid;

		uint16_t handle = 0;
	};

	// NimBLE host task: stores the connect, disconnect and encryption events; accepts the rest (a security request gets NimBLE's
	// Just Works pairing, without a bond)
	static int onGapEvent(ble_gap_event* event, void* arg);

	static int onService(uint16_t connHandle, const ble_gatt_error* error, const ble_gatt_svc* service, void* arg);

	static int onCharacteristic(uint16_t connHandle, const ble_gatt_error* error, const ble_gatt_chr* characteristic, void* arg);

	// NimBLE host task: a part of the value, its end or an error. A non-zero return ends the read, and NimBLE calls no more.
	static int onValue(uint16_t connHandle, const ble_gatt_error* error, ble_gatt_attr* attr, void* arg);

	// NimBLE host task, under the lock: a GATT request ended; 0 and BLE_HS_EDONE mean done
	void setRequestResult(int status);

	// handles the connect, encryption and disconnect events stored since the last update()
	void handleGapEvents();

	// does the work of the current state
	void updateState(uint32_t dtMs);

	// Requested: tries once to acquire the radio and connects when it has it; Failed after BleConnection::kRadioWaitMs without it
	void startProbe(uint32_t dtMs);

	void connect();

	void onConnected(int status, uint16_t newConnHandle);

	void onDisconnected(int reason, uint16_t oldConnHandle);

	// logs how the pairing the device asked for ended
	void onEncryptionChanged(int status);

	void discoverServices();

	// after the services, or after a service's characteristics: discovers the next service's characteristics, else reads the values
	void discoverNextService();

	// adds the service's characteristics to the table, and the ones to read to valuesToRead
	void addCharacteristics();

	// reads the next value, else disconnects
	void readNextValue();

	// adds the value read to the table; a value not read (the device may guard it, the probe never pairs for it) is logged
	void storeValue(int error);

	// Logs why the probe failed and disconnects; ends in Failed.
	void fail(const std::string& why);

	// Closes the connection; finishes at once when there is none.
	void disconnect();

	// Disconnecting: finishes after BleConnection::kCloseWaitMs even without the disconnect event
	void waitForDisconnect(uint32_t dtMs);

	// the connection is down: Done with the table, or Failed
	void finishDisconnect();

	// Resets the request to Running with nothing received; before each GATT call, whose callbacks may come at once.
	void startRequest();

	// Checks the GATT request: false while it runs; true when it ended, with error 0 or NimBLE's error.
	bool checkRequest(int& error);

	bool isConnected() const;

	void setState(State next);

private:
	// protects the fields below that the web server or NimBLE's callbacks use
	mutable FreeRtosMutex lock;

	BleScanner& scanner;

	// setup() found NimBLE running
	bool nimBleRunning = false;

	uint8_t ownAddressType = 0;

	// request() changes it from Idle, Done and Failed, update() in the other states
	State state = State::Idle;

	// written by request() in Idle, Done and Failed only
	BleAddress peer;

	// written with peer
	BleDeviceBase::AddressType peerType = BleDeviceBase::AddressType::Public;

	// time spent waiting for the radio; reset by request(), counted by update() while Requested
	uint32_t radioWaitedMs = 0;

	// the table of the last probe that ended Done; read() copies it under the lock
	GattTable finishedTable;

	// application thread only: the services, the table being read, the values to read, the service or value being worked on
	std::vector<Service> services;

	GattTable tableInProgress;

	std::vector<ValueToRead> valuesToRead;

	size_t index = 0;

	// the probe failed; it ends in Failed once disconnected
	bool failed = false;

	uint32_t closeWaitedMs = 0;

	// the fields below are used by NimBLE's callbacks, under the lock
	uint16_t connHandle = 0;

	// a connection exists: connected and not yet disconnected
	bool connected = false;

	GattRequest gattRequest;

	bool connectEvent = false;

	int connectStatus = 0;

	uint16_t eventConnHandle = 0;

	bool disconnectEvent = false;

	int disconnectReason = 0;

	uint16_t disconnectConnHandle = 0;

	bool encryptionEvent = false;

	int encryptionStatus = 0;
};

}  // namespace w2b
