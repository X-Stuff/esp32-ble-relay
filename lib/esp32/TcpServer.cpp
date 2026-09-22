#include "TcpServer.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#include <fcntl.h>
#include <lwip/sockets.h>
#include <unistd.h>

#include "Logger.h"
#include "WriteMode.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "tcp";

// bytes one recv() takes
constexpr size_t kReceiveChunk = 256;

bool isWouldBlock(int error)
{
	return error == EAGAIN || error == EWOULDBLOCK;
}

bool makeNonBlocking(int socket)
{
	const int flags = fcntl(socket, F_GETFL, 0);

	return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}

TcpStatus::ClientAddress formatAddress(const sockaddr_in& address)
{
	const uint32_t ip = ntohl(address.sin_addr.s_addr);
	char text[TcpStatus::ClientAddress::kCapacity + 1];
	snprintf(text, sizeof(text), "%u.%u.%u.%u:%u", (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 0xff), (unsigned)((ip >> 8) & 0xff),
		(unsigned)(ip & 0xff), (unsigned)ntohs(address.sin_port));

	return TcpStatus::ClientAddress(text);
}

}  // namespace

TcpServer::TcpServer(const RuleConfig& rule, const RxRing* ring, FreeRtosMutex& lock, RelayCounters& counters)
	: rule(rule), ring(ring), lock(lock), counters(counters), retryMs(kListenRetryMs)
{
}

TcpServer::~TcpServer()
{
	closeClients("the relay stopped");
	closeListener();
}

size_t TcpServer::clientCount() const
{
	return std::count_if(clients.begin(), clients.end(),
		[](const Client& client)
		{
			return client.socket >= 0;
		});
}

void TcpServer::update(uint32_t dtMs, bool linkUp, uint16_t mtu)
{
	if (listener < 0)
	{
		retryMs += dtMs;

		if (retryMs >= kListenRetryMs)
		{
			retryMs = 0;
			openListener();
		}
	}

	if (listener >= 0)
	{
		acceptClients();
	}

	const size_t chunkBytes = rule.oversize() == RuleConfig::Oversize::Chunk && linkUp ? maxWriteBytes(mtu) : 0;

	for (Client& client : clients)
	{
		if (client.socket < 0)
		{
			continue;
		}

		client.sawLink = client.sawLink || linkUp;
		receive(client);

		// receive() closes a client that left
		if (client.socket >= 0)
		{
			client.data->update(dtMs, rule.coalesceMs(), chunkBytes);
			dropOldPackets(client, linkUp);
			sendPackets(client, dtMs);
		}

		// sendPackets() closes a client that does not read
		if (client.socket >= 0 && client.data->empty())
		{
			logDropped(client);
		}
	}
}

TcpClientData* TcpServer::nextPacket(uint32_t& clientId)
{
	for (size_t i = 0; i < clients.size(); ++i)
	{
		const size_t index = (nextClient + i) % clients.size();
		Client& client = clients[index];

		if (client.socket >= 0 && client.data->hasPacket())
		{
			nextClient = (index + 1) % clients.size();
			clientId = client.id;
			return client.data.get();
		}
	}

	return nullptr;
}

TcpClientData* TcpServer::clientData(uint32_t clientId)
{
	for (Client& client : clients)
	{
		if (client.socket >= 0 && client.id == clientId)
		{
			return client.data.get();
		}
	}

	return nullptr;
}

void TcpServer::closeWaitingClients(const char* why)
{
	for (Client& client : clients)
	{
		if (client.socket >= 0 && !client.sawLink)
		{
			closeClient(client, why);
		}
	}
}

void TcpServer::closeClients(const char* why)
{
	for (Client& client : clients)
	{
		if (client.socket >= 0)
		{
			closeClient(client, why);
		}
	}
}

TcpStatus TcpServer::status() const
{
	TcpStatus::Clients addresses{};
	size_t count = 0;

	for (const Client& client : clients)
	{
		if (client.socket >= 0)
		{
			addresses[count++] = client.address;
		}
	}

	return TcpStatus(listener >= 0, lastError, addresses, count);
}

// Bound to every address of the relay; SO_REUSEADDR lets a restarted relay take its port at once.
void TcpServer::openListener()
{
	const int listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	int error = errno;

	if (listenSocket >= 0)
	{
		const int yes = 1;
		setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

		const auto address = sockaddr_in
		{
			.sin_family = AF_INET,
			.sin_port = htons(rule.listenPort()),
			.sin_addr = {.s_addr = htonl(INADDR_ANY)}
		};

		if (bind(listenSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 && listen(listenSocket, RuleConfig::kMaxClients) == 0 &&
			makeNonBlocking(listenSocket))
		{
			listener = listenSocket;
			lastError = "";
			Logger::info(kTag, "relay %s: listening on port %u, up to %u clients", rule.id().c_str(), (unsigned)rule.listenPort(),
				(unsigned)rule.maxClients());
			return;
		}

		error = errno;
		close(listenSocket);
	}

	char text[TcpStatus::Error::kCapacity + 1];
	snprintf(text, sizeof(text), "port %u: %s", (unsigned)rule.listenPort(), strerror(error));

	// once per reason, not at every retry
	if (!(lastError == text))
	{
		lastError = text;
		Logger::warn(kTag, "relay %s: not listening, %s; next try in %u s", rule.id().c_str(), text, (unsigned)(kListenRetryMs / 1000));
	}
}

void TcpServer::acceptClients()
{
	for (;;)
	{
		sockaddr_in from{};
		socklen_t size = sizeof(from);
		const int clientSocket = accept(listener, reinterpret_cast<sockaddr*>(&from), &size);

		if (clientSocket >= 0)
		{
			addClient(clientSocket, formatAddress(from));
			continue;
		}

		const int error = errno;

		// lwIP drops a connection it has no socket for
		if (!isWouldBlock(error))
		{
			lastError = (std::string("a client was refused: ") + strerror(error)).c_str();
			Logger::warn(kTag, "relay %s: %s", rule.id().c_str(), lastError.c_str());
		}

		return;
	}
}

void TcpServer::addClient(int clientSocket, const TcpStatus::ClientAddress& address)
{
	// the rule's max_clients slots of the kMaxClients
	const auto end = clients.begin() + std::min<size_t>(rule.maxClients(), clients.size());
	auto slot = std::find_if(clients.begin(), end,
		[](const Client& client)
		{
			return client.socket < 0;
		});

	std::unique_ptr<TcpClientData> data(slot != end ? new (std::nothrow) TcpClientData() : nullptr);

	if (!data || !makeNonBlocking(clientSocket))
	{
		close(clientSocket);
		lastError = (std::string("a client from ") + address.c_str() + " was refused: " +
			(slot == end ? "max_clients " + std::to_string(rule.maxClients()) : std::string("no memory"))).c_str();
		Logger::warn(kTag, "relay %s: %s", rule.id().c_str(), lastError.c_str());
		return;
	}

	// small packets go out at once, without waiting for more
	const int yes = 1;
	setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

	// a client that stops answering is reset by lwIP after the probes, its send queue freed at once
	const int idleS = kKeepAliveIdleS;
	const int intervalS = kKeepAliveIntervalS;
	const int count = kKeepAliveCount;
	setsockopt(clientSocket, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
	setsockopt(clientSocket, IPPROTO_TCP, TCP_KEEPIDLE, &idleS, sizeof(idleS));
	setsockopt(clientSocket, IPPROTO_TCP, TCP_KEEPINTVL, &intervalS, sizeof(intervalS));
	setsockopt(clientSocket, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));

	*slot = Client
	{
		.socket = clientSocket,
		.id = nextId++,
		.address = address,
		.data = std::move(data)
	};

	if (ring != nullptr)
	{
		LockGuard guard(&lock);
		slot->reader = ring->createReader();
	}

	lastError = "";
	Logger::info(kTag, "relay %s: client %s connected, %u of %u", rule.id().c_str(), address.c_str(), (unsigned)clientCount(),
		(unsigned)rule.maxClients());
}

void TcpServer::receive(Client& client)
{
	const bool noTx = rule.tx().mode() == ChannelMode::None;
	uint8_t buffer[kReceiveChunk];

	for (;;)
	{
		const size_t space = client.data->spaceLeft();
		const bool discard = noTx || space == 0;
		const ssize_t got = recv(client.socket, buffer, discard ? sizeof(buffer) : std::min(space, sizeof(buffer)), MSG_DONTWAIT);

		if (got > 0)
		{
			counters.countTcpRx(got);

			if (noTx)
			{
				counters.countTxDropped(got);
			}
			else if (space == 0)
			{
				dropOverflow(client, got);
			}
			else
			{
				client.data->add(buffer, got);
			}

			continue;
		}

		const int error = errno;

		if (got == 0 || !isWouldBlock(error))
		{
			closeClient(client, got == 0 ? "closed by the client" : strerror(error));
		}

		return;
	}
}

void TcpServer::dropOverflow(Client& client, size_t bytes)
{
	counters.countTxDropped(bytes);

	if (client.droppedBytes == 0)
	{
		Logger::warn(kTag, "relay %s: client %s: its buffer is full, its further bytes are dropped until the link makes room", rule.id().c_str(),
			client.address.c_str());
	}

	client.droppedBytes += bytes;
}

void TcpServer::logDropped(Client& client)
{
	if (client.droppedBytes == 0)
	{
		return;
	}

	Logger::info(kTag, "relay %s: %u B from %s dropped: its buffer was full", rule.id().c_str(), (unsigned)client.droppedBytes, client.address.c_str());
	client.droppedBytes = 0;
}

void TcpServer::dropOldPackets(Client& client, bool linkUp)
{
	TcpClientData& data = *client.data;

	if (linkUp || !data.hasPacket() || data.waitedMs() < kQueueTimeoutMs)
	{
		return;
	}

	size_t bytes = 0;

	while (data.hasPacket())
	{
		bytes += data.peek(nullptr, 0, 0);
		data.pop();
	}

	counters.countTxDropped(bytes);
	Logger::info(kTag, "relay %s: %u B from %s dropped: no BLE link within %u s", rule.id().c_str(), (unsigned)bytes, client.address.c_str(),
		(unsigned)(kQueueTimeoutMs / 1000));
}

// lwIP takes part of the bytes or none while the socket's send queue is full; the rest waits in the ring, the reader keeping its place.
void TcpServer::sendPackets(Client& client, uint32_t dtMs)
{
	uint8_t packet[kMaxAttributeValue];

	while (ring != nullptr)
	{
		size_t left = 0;
		uint32_t lost = 0;

		{
			LockGuard guard(&lock);
			const uint32_t before = client.reader.lost();
			left = ring->peek(client.reader, packet, sizeof(packet));
			lost = client.reader.lost() - before;
		}

		counters.countRxDropped(lost);

		if (left == 0)
		{
			client.stalledMs = 0;
			return;
		}

		const size_t length = std::min(left, sizeof(packet));
		const ssize_t sent = send(client.socket, packet, length, MSG_DONTWAIT);
		const int error = errno;

		if (sent < 0 && !isWouldBlock(error))
		{
			closeClient(client, strerror(error));
			return;
		}

		if (sent <= 0)
		{
			stallClient(client, dtMs);
			return;
		}

		client.stalledMs = 0;
		counters.countTcpTx(sent);

		{
			LockGuard guard(&lock);
			const uint32_t before = client.reader.lost();
			ring->skip(client.reader, sent);
			lost = client.reader.lost() - before;
		}

		counters.countRxDropped(lost);

		if (static_cast<size_t>(sent) < length)
		{
			return;
		}
	}
}

void TcpServer::stallClient(Client& client, uint32_t dtMs)
{
	client.stalledMs += dtMs;

	if (client.stalledMs < kSendStallMs)
	{
		return;
	}

	char why[64];
	snprintf(why, sizeof(why), "it does not read, its socket took nothing for %u s", (unsigned)(kSendStallMs / 1000));
	closeClient(client, why);
}

void TcpServer::closeClient(Client& client, const char* why)
{
	logDropped(client);
	close(client.socket);
	Logger::info(kTag, "relay %s: client %s closed: %s", rule.id().c_str(), client.address.c_str(), why);

	client.socket = -1;
	client.data.reset();
	client.reader = RxRing::Reader();
	client.sawLink = false;
	client.stalledMs = 0;
}

void TcpServer::closeListener()
{
	if (listener < 0)
	{
		return;
	}

	close(listener);
	listener = -1;
	Logger::info(kTag, "relay %s: stopped listening on port %u", rule.id().c_str(), (unsigned)rule.listenPort());
}

}  // namespace w2b
