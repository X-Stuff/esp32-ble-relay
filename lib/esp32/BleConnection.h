#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BleDeviceBase.h"
#include "ChannelHandles.h"
#include "DeviceRules.h"
#include "FreeRtosMutex.h"
#include "RelayStatus.h"
#include "TcpServer.h"
#include "WriteMode.h"

struct ble_gap_event;
struct ble_gatt_error;
struct ble_gatt_svc;
struct ble_gatt_chr;
struct ble_gatt_dsc;
struct ble_gatt_attr;

namespace w2b
{

class BleScanner;

// One device's BLE connection and its rules' relays: searches the device, connects, subscribes the rules' RX and writes their TCP
// clients' packets to TX, with NimBLE's C calls, none of which block. update() does the work; NimBLE's callbacks only store data.
class BleConnection
{
public:
	// how long a connect waits for the device
	static constexpr uint32_t kConnectTimeoutMs = 10000;

	// how often the RSSI is read while the connection is up
	static constexpr uint32_t kRssiPeriodMs = 5000;

	// how long a disconnect waits for NimBLE's event before the connection counts as closed: the longest supervision timeout
	// Bluetooth allows, within which a disconnect ends even when the device no longer answers
	static constexpr uint32_t kCloseWaitMs = 32000;

	// how long a probe or a page scan waits for the radio before it fails: longer than a connect, the longest a connection holds it
	static constexpr uint32_t kRadioWaitMs = 2 * kConnectTimeoutMs;

	// no device; call init() before use
	BleConnection() = default;

	BleConnection(const BleConnection&) = delete;

	BleConnection& operator=(const BleConnection&) = delete;

	// Sets the scanner (device search and radio) and NimBLE's type of the relay's own address. Called once.
	void init(BleScanner& scanner, uint8_t ownAddressType);

	// the device and its rules, set by setDevice(); none after clearDevice()
	inline const std::optional<DeviceRules>& device() const { return deviceRules; }

	// true when it has no device and no open connection
	bool isFree() const;

	// true while its device is removed and its connection still closes
	bool isClosing() const;

	// Sets the device and starts its rules' TCP servers; it connects while a rule wants the link. Only when isFree().
	void setDevice(const DeviceRules& device);

	// Removes the device: its rules' TCP servers close at once, then the connection closes.
	void clearDevice();

	// Runs the connection's state machine, the rules' TCP servers and the TX writes; called by the relay thread.
	void update(uint32_t dtMs);

	// Reads the status of rule id into status; false when id is not one of the device's rules.
	bool readStatus(const RuleConfig::Id& id, std::optional<RelayStatus>& status) const;

private:
	// State of the connection; the status shows fewer states (LinkState)
	enum class State : uint8_t
	{
		Idle,
		Searching,
		Connecting,
		ExchangingMtu,
		DiscoveringServices,
		DiscoveringCharacteristics,
		DiscoveringCccds,
		Subscribing,
		Up,
		Disconnecting,
		Waiting,
	};

	// State of the last GATT request, set by its callback
	enum class RequestState : uint8_t
	{
		None,
		Running,
		Done,
		Failed,
	};

	// one of the device's rules on this connection, and its relay
	struct Rule
	{
		// core: the rule, its channels and its relay's data

		RuleConfig config;

		ChannelHandles handles;

		uint16_t cccd = 0;

		// why this rule does not run while the connection is up
		RelayStatus::Error error;

		// its RX frames go into the ring: from the channels found until the disconnect
		bool receiving = false;

		// the RX packets for the TCP clients, while the device is set; none without RX
		std::unique_ptr<TcpServer::RxRing> ring;

		// on demand: the link stays wanted until idle_s after the last client left
		bool holdLink = false;

		uint32_t idleMs = 0;

		// statistics: for the status and the log only

		// the hexdump's place in the ring
		TcpServer::RxRing::Reader logReader;

		RelayCounters counters;

		// last: it keeps references to the ring and the counters
		std::unique_ptr<TcpServer> server;
	};

	// the TCP packet being written to a rule's TX
	struct TxWrite
	{
		bool running = false;

		size_t rule = 0;

		uint32_t client = 0;

		WriteMode mode = WriteMode::Single;

		size_t size = 0;

		// bytes written so far, and in the write in flight
		size_t offset = 0;

		size_t piece = 0;

		// a write with response or a long write waits for its callback
		bool waiting = false;
	};

	// a primary service of the device
	struct Service
	{
		BleUuid uuid;

		uint16_t start = 0;

		uint16_t end = 0;

		std::vector<DiscoveredCharacteristic> characteristics;
	};

	// a characteristic as NimBLE reports it, before its end handle is known
	struct NimBleCharacteristic
	{
		BleUuid uuid;

		uint8_t properties = 0;

		uint16_t declarationHandle = 0;

		uint16_t valueHandle = 0;
	};

	// NimBLE host task
	static int onGapEvent(ble_gap_event* event, void* arg);

	static int onMtu(uint16_t connHandle, const ble_gatt_error* error, uint16_t mtu, void* arg);

	static int onService(uint16_t connHandle, const ble_gatt_error* error, const ble_gatt_svc* service, void* arg);

	static int onCharacteristic(uint16_t connHandle, const ble_gatt_error* error, const ble_gatt_chr* characteristic, void* arg);

	static int onDescriptor(uint16_t connHandle, const ble_gatt_error* error, uint16_t valueHandle, const ble_gatt_dsc* descriptor, void* arg);

	static int onWrite(uint16_t connHandle, const ble_gatt_error* error, ble_gatt_attr* attr, void* arg);

	// NimBLE host task, under the lock: a GATT request ended; 0 and BLE_HS_EDONE mean done
	void setRequestResult(int status);

	// NimBLE host task, under the lock: adds an indication or notification to the rings of the rules with that RX handle
	void queueRx(uint16_t attrHandle, const uint8_t* data, size_t size);

	// handles the connect and disconnect events stored since the last update()
	void handleGapEvents();

	// does the work of the current state
	void updateState(uint32_t dtMs);

	void startSearch();

	void checkSearch();

	void connect();

	void onConnected(int status, uint16_t newConnHandle);

	void onDisconnected(int reason, uint16_t oldConnHandle);

	void discoverServices();

	// after the services, or after a service's characteristics: discovers the next service a rule needs, else finds the channels
	void discoverNextService();

	// true when service is a rule's service
	bool isRuleService(const Service& service) const;

	void findChannels();

	// discovers the next RX's CCCD, else starts the subscriptions
	void discoverNextCccd();

	// writes the next RX's CCCD, else finishes the setup
	void subscribeNext();

	void finishSetup();

	// Stores why the attempt failed and waits the backoff; an open connection is closed first. Closes the TCP clients that never saw
	// the link once the next wait is the backoff's last, and on a lost link those of rules with close_tcp_on_ble_loss.
	void fail(const std::string& why);

	// Closes the connection: Idle once it is down, or Waiting when retry.
	void disconnect(bool retry);

	// Disconnecting: NimBLE's event ends it; without one the connection counts as closed after kCloseWaitMs
	void waitForDisconnect(uint32_t dtMs);

	// the connection is down: Waiting before the next attempt when the disconnect retries, else Idle, logged when a link was open
	void finishDisconnect();

	// creates the rule's ring and TCP server; the rules' vector is complete, since the server keeps references into it
	void startRelay(Rule& rule);

	// a rule's policy is always, it has clients, or it holds the link for idle_s
	bool isLinkWanted() const;

	// on demand: holds each rule's link while it has clients and idle_s after the last one left
	void updateIdleTimes(uint32_t dtMs);

	void updateServers(uint32_t dtMs);

	// while up: goes on with the packet being written, or starts the next, round robin over the rules
	void writeTx();

	void startTx();

	// issues the next write of the packet: a piece of it, or all of it as a long write
	void sendTx();

	// the write in flight ended: the next piece, or the packet is done
	void continueTx();

	// the packet is out of the client's data: written, or dropped and counted
	void finishTx(bool written);

	// logs the RX frames queued since the last update() for the rules with hexdump
	void logRxFrames();

	// logs the first kHexdumpBytes of frame, which has size bytes, kHexdumpLineBytes a line
	void logHexdump(const Rule& rule, const uint8_t* frame, size_t size) const;

	// Moves rule's hexdump reader to its next frame, copied to out, at most room bytes; returns its size, 0 when none waits.
	size_t readLogFrame(Rule& rule, uint8_t* out, size_t room);

	void readRssi(uint32_t dtMs);

	// Checks the GATT request: false while it runs; true when it ended, with error 0 or NimBLE's error, logged with what.
	bool checkRequest(const char* what, int& error);

	void setRequestState(RequestState next);

	// the negotiated ATT MTU, under the lock; 0 while down
	uint16_t currentMtu() const;

	LinkState linkState() const;

private:
	// core: the device, its rules, the connection, the TX write, the discovery and what NimBLE's callbacks report

	// protects what NimBLE's callbacks write: the events, the request results, the rings and the RX counters
	mutable FreeRtosMutex lock;

	BleScanner* scanner = nullptr;

	uint8_t ownAddressType = 0;

	std::optional<DeviceRules> deviceRules;

	std::vector<Rule> rules;

	State state = State::Idle;

	// the search's slot in the scanner while Searching
	size_t searchSlot = 0;

	// the device found: address, address type and RSSI
	std::optional<BleDeviceBase> foundDevice;

	uint16_t connHandle = 0;

	// a connection exists: connected and not yet disconnected
	bool connected = false;

	// ble_gap_connect() runs: its connect or disconnect event ends it; the radio is the connection's until then
	bool connectPending = false;

	uint16_t mtu = 0;

	// failed attempts in a row, for the backoff
	uint32_t failedAttempts = 0;

	uint32_t retryWaitMs = 0;

	uint32_t waitedMs = 0;

	// retry after the disconnect finishes
	bool retryAfterDisconnect = false;

	TxWrite tx;

	// the rule startTx() looks at first
	size_t nextTxRule = 0;

	std::vector<Service> services;

	// the service or rule the discovery works on
	size_t index = 0;

	std::vector<NimBleCharacteristic> serviceCharacteristics;

	uint16_t foundCccd = 0;

	// written by the callbacks under the lock
	RequestState requestState = RequestState::None;

	int requestError = 0;

	bool connectEvent = false;

	int connectStatus = 0;

	uint16_t eventConnHandle = 0;

	bool disconnectEvent = false;

	int disconnectReason = 0;

	uint16_t disconnectConnHandle = 0;

private:
	// statistics: for the status and the log only

	// the link was up since setDevice(): a new link is a reconnect
	bool wasUp = false;

	RelayStatus::Error lastError;

	// dBm, the last reading while the link was up; kept after the link drops, 0 until the first reading of a device
	int8_t rssi = 0;

	uint32_t sinceRssiMs = 0;

	uint32_t upMs = 0;
};

}  // namespace w2b
