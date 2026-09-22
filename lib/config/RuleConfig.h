#pragma once

#ifndef HTTP_PORT
#error "HTTP_PORT comes from platformio.ini's [common] build_flags"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "BleChannelConfig.h"
#include "BleUuid.h"
#include "FixedString.h"
#include "JsonSerializable.h"
#include "TargetConfig.h"

namespace w2b
{

// One relay, an entry of "rules" in /config.json: a TCP port on the LAN bridged to a characteristic pair of one BLE device. Has no
// valid default: the empty object exists only for fromJson and holds the defaults of the keys a request may leave out.
class RuleConfig : public IJsonSerializable<RuleConfig>
{
public:
	// the listen_port of a new rule; the page takes the first free port from here up
	static constexpr uint16_t kDefaultPort = 8899;

	// the web server's port (platformio.ini), never a listen_port
	static constexpr uint16_t kWebPort = HTTP_PORT;

	// the most TCP clients of one rule
	static constexpr uint8_t kMaxClients = 4;

	// the longest coalescing silence
	static constexpr uint16_t kMaxCoalesceMs = 1000;

	// the longest idle time before an on-demand disconnect
	static constexpr uint16_t kMaxIdleS = 3600;

	// the most reconnect waits
	static constexpr size_t kMaxBackoffSteps = 10;

	// the longest reconnect wait
	static constexpr uint16_t kMaxBackoffS = 3600;

	// part of the rule's URLs, /api/rules/{id}; ConfigData assigns "id0", "id1", ...
	using Id = FixedString<32>;

	// for people
	using Name = FixedString<63>;

	// the waits between BLE reconnect attempts in seconds, reconnectSteps() of them; the unused entries are 0
	using Backoff = std::array<uint16_t, kMaxBackoffSteps>;

	// What happens to TCP data larger than a write takes: the negotiated ATT MTU - 3 bytes
	enum class Oversize : uint8_t
	{
		// consecutive writes
		Chunk,

		// one ATT prepare/execute write, one value at the device
		LongWrite,

		// discarded and counted
		Drop,
	};

	// When BLE connects
	enum class ConnectPolicy : uint8_t
	{
		// with the first TCP client, until idle_s after the last one left
		OnDemand,

		// while the rule is enabled
		Always,
	};

	// every field, as their getters describe them
	RuleConfig(const Id& id, const Name& name, bool enabled, uint16_t listenPort, uint8_t maxClients, const TargetConfig& target,
		const BleUuid& serviceUuid, const TxConfig& tx, const RxConfig& rx, uint16_t coalesceMs, Oversize oversize,
		ConnectPolicy connectPolicy, uint16_t idleS, const Backoff& reconnectBackoffS, bool closeTcpOnBleLoss, bool hexdump);

private:
	friend class SerializableFactory;

	// assigns the ids
	friend class ConfigData;

	RuleConfig() = default;

public:
	// unique among the rules; empty until ConfigData assigns one
	inline const Id& id() const { return ident; }

	// empty: the id stands in
	inline const Name& name() const { return label; }

	// runs; at most Config's kMaxActiveRules are
	inline bool enabled() const { return active; }

	// TCP port on the LAN, unique among the enabled rules
	inline uint16_t listenPort() const { return port; }

	// TCP clients at a time; more are refused
	inline uint8_t maxClients() const { return clientLimit; }

	// the BLE device; rules on other characteristics of it may share it
	inline const TargetConfig& target() const { return peer; }

	// the GATT service that holds tx and rx
	inline const BleUuid& serviceUuid() const { return service; }

	// the characteristic TCP data is written to
	inline const TxConfig& tx() const { return txChannel; }

	// the characteristic whose values go to the TCP clients
	inline const RxConfig& rx() const { return rxChannel; }

	// silence that ends a TCP to BLE write; 0: none
	inline uint16_t coalesceMs() const { return coalesceWindowMs; }

	// TCP data larger than a write takes
	inline Oversize oversize() const { return oversizePolicy; }

	// when BLE connects
	inline ConnectPolicy connectPolicy() const { return policy; }

	// with OnDemand: BLE stays up this long after the last client left
	inline uint16_t idleS() const { return idleTimeS; }

	// waits between BLE reconnect attempts; the last one repeats
	inline const Backoff& reconnectBackoffS() const { return backoff; }

	// TCP clients are closed when BLE is lost
	inline bool closeTcpOnBleLoss() const { return closeOnLoss; }

	// every TCP to BLE write and BLE to TCP value in the log as hex
	inline bool hexdump() const { return dumpHex; }

	// the waits of reconnectBackoffS() in use: those before the first 0
	size_t reconnectSteps() const;

	// Every field fits and is in range; the first problem as "field: reason". Checks across rules are Config's.
	bool validate(std::string& error) const;

	// every field, in the order of the file
	void toJson(JsonObject& json) const override;

	// target and service_uuid required, id optional; keys the JSON leaves out keep their value.
	bool fromJson(const JsonObjectConst& json, std::string& error) override;

	// every field equal
	bool operator==(const RuleConfig& other) const;

private:
	void assignId(const Id& id);

	bool readIdentity(const JsonObjectConst& json, std::string& error);

	bool readLink(const JsonObjectConst& json, std::string& error);

	bool readBehaviour(const JsonObjectConst& json, std::string& error);

	bool readBackoff(const JsonObjectConst& json, std::string& error);

	bool validateIdentity(std::string& error) const;

	bool validateLink(std::string& error) const;

	bool validateTiming(std::string& error) const;

	Id ident;

	Name label;

	bool active = true;

	uint16_t port = kDefaultPort;

	uint8_t clientLimit = 1;

	TargetConfig peer = SerializableFactory::createEmpty<TargetConfig>();

	BleUuid service;

	TxConfig txChannel;

	RxConfig rxChannel;

	uint16_t coalesceWindowMs = 10;

	Oversize oversizePolicy = Oversize::Chunk;

	ConnectPolicy policy = ConnectPolicy::OnDemand;

	uint16_t idleTimeS = 3;

	Backoff backoff = {2, 4, 8, 10, 30};

	bool closeOnLoss = false;

	bool dumpHex = false;
};

}  // namespace w2b
