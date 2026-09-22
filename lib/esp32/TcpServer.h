#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "FreeRtosMutex.h"
#include "PacketRing.h"
#include "RelayStatus.h"
#include "RuleConfig.h"
#include "TcpClientData.h"

namespace w2b
{

// One rule's TCP server on non-blocking lwIP sockets: a listener on the rule's port and up to max_clients clients. It sends every packet
// of the rule's RX ring to every client and collects each client's bytes into packets for the BLE writes. The relay thread's.
class TcpServer
{
public:
	// a rule's queue from BLE to its TCP clients, packets with their lengths
	static constexpr size_t kRxRingBytes = 1024;

	// the rule's packets from BLE; every client reads them all
	using RxRing = PacketRing<WhenFull::DropOldest, kRxRingBytes>;

	// how long a listener that did not open waits before the next try
	static constexpr uint32_t kListenRetryMs = 5000;

	// how long a client's packet waits for the BLE link before it is dropped
	static constexpr uint32_t kQueueTimeoutMs = 10000;

	// a client that sent nothing for this long gets keepalive probes
	static constexpr uint32_t kKeepAliveIdleS = 5;

	// between keepalive probes
	static constexpr uint32_t kKeepAliveIntervalS = 2;

	// unanswered probes after which lwIP resets the connection and frees its queues at once
	static constexpr uint32_t kKeepAliveCount = 3;

	// How long a client's socket may take nothing while RX bytes wait for it before the client counts as not reading and is closed.
	// Longer than the keepalive's deadline: a vanished client is reset by it; closed first it would linger half-open with its queue.
	static constexpr uint32_t kSendStallMs = 15000;

	static_assert(kSendStallMs > (kKeepAliveIdleS + kKeepAliveCount * kKeepAliveIntervalS) * 1000, "keepalive must reset a vanished client first");

	// rule: port, clients, coalesce_ms, TX; ring: the RX packets, read under lock, null without RX; counters: the rule's traffic
	TcpServer(const RuleConfig& rule, const RxRing* ring, FreeRtosMutex& lock, RelayCounters& counters);

	~TcpServer();

	TcpServer(const TcpServer&) = delete;

	TcpServer& operator=(const TcpServer&) = delete;

	// clients connected
	size_t clientCount() const;

	// Opens the listener when it is not, accepts, receives and sends. linkUp and mtu: the rule's BLE link, for chunks and waiting.
	void update(uint32_t dtMs, bool linkUp, uint16_t mtu);

	// The next client with a packet for BLE, round robin, and its id in clientId; null when no packet waits.
	TcpClientData* nextPacket(uint32_t& clientId);

	// the data of the client clientId; null when it is gone
	TcpClientData* clientData(uint32_t clientId);

	// Closes every client that has not seen the link up since it connected; why goes into the log.
	void closeWaitingClients(const char* why);

	// Closes every client; why goes into the log.
	void closeClients(const char* why);

	// the listener, the last refusal and the clients
	TcpStatus status() const;

private:
	// a TCP client's socket, its bytes for BLE and its place in the RX ring
	struct Client
	{
		int socket = -1;

		uint32_t id = 0;

		TcpStatus::ClientAddress address;

		std::unique_ptr<TcpClientData> data;

		RxRing::Reader reader;

		// the link was up while it was connected
		bool sawLink = false;

		// since its socket last took a byte of the RX bytes waiting for it
		uint32_t stalledMs = 0;

		// bytes dropped since its buffer last ran empty; logged once it does, and when the client leaves
		size_t droppedBytes = 0;
	};

	void openListener();

	void acceptClients();

	// a client from address: a free slot and its data, or it is closed at once
	void addClient(int socket, const TcpStatus::ClientAddress& address);

	// Reads the client's bytes into its data; without TX or without room they are read and dropped, counted: left in the socket they
	// would hold lwIP's mailbox and WiFi's receive buffers, few for the whole relay.
	void receive(Client& client);

	// the client's buffer is full: bytes of it are dropped, counted, one log line when it starts
	void dropOverflow(Client& client, size_t bytes);

	// logs the bytes dropped from the client since its buffer last ran empty, once it did or the client leaves
	void logDropped(Client& client);

	// drops and counts the client's packets once the oldest has waited kQueueTimeoutMs without the link
	void dropOldPackets(Client& client, bool linkUp);

	// sends the RX bytes the client has not had, as many as its socket takes; closes it when that is none for kSendStallMs
	void sendPackets(Client& client, uint32_t dtMs);

	// the client's socket took nothing now: counts the time, and closes the client after kSendStallMs
	void stallClient(Client& client, uint32_t dtMs);

	void closeClient(Client& client, const char* why);

	void closeListener();

private:
	const RuleConfig rule;

	const RxRing* ring;

	FreeRtosMutex& lock;

	RelayCounters& counters;

	int listener = -1;

	// since the listener did not open
	uint32_t retryMs = 0;

	// why the listener does not run, or the last client refused
	TcpStatus::Error lastError;

	std::array<Client, RuleConfig::kMaxClients> clients{};

	uint32_t nextId = 1;

	// where nextPacket() looks first
	size_t nextClient = 0;
};

}  // namespace w2b
