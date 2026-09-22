// A relay's state for the page: its BLE connection, its TCP server and the traffic between them. Not persisted: toJson only, for the
// HTTP API.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "BleAddress.h"
#include "FixedString.h"
#include "JsonForward.h"
#include "RuleConfig.h"

namespace w2b
{

// State of a device's BLE connection
enum class LinkState : uint8_t
{
	// the rule is disabled
	Disabled,

	// no rule needs the connection: on demand, and no TCP client
	Idle,

	// scanning for the device's advertisement
	Searching,

	// connecting, then exchanging the MTU
	Connecting,

	// discovering the services and characteristics of the rules
	Discovering,

	// writing the CCCDs of the rules' RX
	Subscribing,

	// connected, every RX subscribed
	Up,

	// waiting before the next attempt; the error says why
	Waiting,

	// disconnecting
	Disconnecting,

	// the device lacks what the rule needs; the device's other rules may still run. The error says what is missing
	Error,
};

// "disabled", "idle", "searching", "connecting", "discovering", "subscribing", "up", "waiting", "disconnecting", "error"
const char* linkStateStr(LinkState state);

// A rule's traffic since its relay started
class RelayCounters
{
public:
	// nothing counted
	RelayCounters() = default;

	// indications and notifications received from BLE
	inline uint32_t rxFrames() const { return rxFrameCount; }

	// their bytes
	inline uint32_t rxBytes() const { return rxByteCount; }

	// frames a TCP client missed: too long for the queue, or dropped from it before the client read them
	inline uint32_t rxDropped() const { return rxDropCount; }

	// bytes received from the TCP clients
	inline uint32_t tcpRxBytes() const { return tcpRxByteCount; }

	// bytes sent to the TCP clients
	inline uint32_t tcpTxBytes() const { return tcpTxByteCount; }

	// GATT writes of TCP data to TX
	inline uint32_t bleTxWrites() const { return txWriteCount; }

	// bytes from the TCP clients not written: no TX, no link in time, a failed write, or too long
	inline uint32_t txDropped() const { return txDropCount; }

	// packets longer than a write takes that the oversize policy dropped
	inline uint32_t oversizeEvents() const { return oversizeCount; }

	// times the link came up again after it was lost or failed
	inline uint32_t reconnects() const { return reconnectCount; }

	// a frame of bytes from BLE
	void countRx(size_t bytes);

	// frames a client missed
	void countRxDropped(uint32_t frames);

	// bytes from a client
	void countTcpRx(size_t bytes);

	// bytes to a client
	void countTcpTx(size_t bytes);

	// one GATT write to TX
	void countTxWrite();

	// bytes from a client not written
	void countTxDropped(size_t bytes);

	// a packet the oversize policy dropped
	void countOversize();

	// the link up again
	void countReconnect();

	// "rx_frames", "rx_bytes", "rx_dropped", "tcp_rx_bytes", "tcp_tx_bytes", "ble_tx_writes", "tx_dropped", "oversize_events",
	// "reconnects"
	void toJson(JsonObject& json) const;

private:
	uint32_t rxFrameCount = 0;

	uint32_t rxByteCount = 0;

	uint32_t rxDropCount = 0;

	uint32_t tcpRxByteCount = 0;

	uint32_t tcpTxByteCount = 0;

	uint32_t txWriteCount = 0;

	uint32_t txDropCount = 0;

	uint32_t oversizeCount = 0;

	uint32_t reconnectCount = 0;
};

// A rule's TCP server: whether it listens, why not, and its clients
class TcpStatus
{
public:
	// a client as "192.168.0.10:51234"
	using ClientAddress = FixedString<21>;

	// the clients, the first clientCount() in use
	using Clients = std::array<ClientAddress, RuleConfig::kMaxClients>;

	// why the listener does not run; empty while it does
	using Error = FixedString<95>;

	// not listening, no clients, no error
	TcpStatus() = default;

	// clientCount: of clients in use
	TcpStatus(bool listening, const Error& error, const Clients& clients, size_t clientCount);

	// the listener accepts clients
	inline bool listening() const { return accepting; }

	// why it does not
	inline const Error& error() const { return problem; }

	// the clients, the first clientCount() in use
	inline const Clients& clients() const { return peers; }

	// clients connected
	inline size_t clientCount() const { return peerCount; }

	// "listening", "tcp_error", "clients": ["ip:port", ...]
	void toJson(JsonObject& json) const;

private:
	bool accepting = false;

	Error problem;

	Clients peers{};

	size_t peerCount = 0;
};

// The state of one rule, a disabled one too
class RelayStatus
{
public:
	// why the last attempt failed or the connection was lost; empty when nothing failed
	using Error = FixedString<127>;

	// address: the device's, once found; mtu: 0 while down; rssi: the last reading, 0 before the first; upS: seconds connected, 0 while down
	RelayStatus(const RuleConfig::Id& id, const RuleConfig::Name& name, LinkState state, const std::optional<BleAddress>& address, uint16_t mtu,
		int8_t rssi, uint32_t upS, const Error& error, const RelayCounters& counters, const TcpStatus& tcp);

	// the rule's id
	inline const RuleConfig::Id& id() const { return ruleId; }

	// the rule's name
	inline const RuleConfig::Name& name() const { return ruleName; }

	// the connection's state
	inline LinkState state() const { return linkState; }

	// the device's address; none until found
	inline const std::optional<BleAddress>& address() const { return deviceAddress; }

	// the negotiated ATT MTU; 0 while down
	inline uint16_t mtu() const { return attMtu; }

	// dBm, the last reading while the link was up, kept while down; 0 before the first reading
	inline int8_t rssi() const { return rssiDbm; }

	// seconds connected; 0 while down
	inline uint32_t upS() const { return upTimeS; }

	// why the last attempt failed or the connection was lost
	inline const Error& error() const { return errorText; }

	// the traffic
	inline const RelayCounters& counters() const { return traffic; }

	// the TCP server
	inline const TcpStatus& tcp() const { return server; }

	// "id", "name", "state", "address" ("" for none), "mtu", "rssi", "up_s", "error", the counters' keys and the TCP status' keys
	void toJson(JsonObject& json) const;

private:
	RuleConfig::Id ruleId;

	RuleConfig::Name ruleName;

	LinkState linkState;

	std::optional<BleAddress> deviceAddress;

	uint16_t attMtu;

	int8_t rssiDbm;

	uint32_t upTimeS;

	Error errorText;

	RelayCounters traffic;

	TcpStatus server;
};

}  // namespace w2b
