#pragma once

#include <cstddef>
#include <cstdint>

#include "PacketRing.h"

namespace w2b
{

// One TCP client's bytes on their way to the BLE link: they collect in an open packet, which closes after coalesce_ms of silence, at a
// write's size for chunking, or when the buffer is full; the closed packets wait for the BLE writes.
class TcpClientData
{
public:
	// the client's buffer, the packets' lengths included
	static constexpr size_t kCapacity = 1024;

	// the longest packet
	static constexpr size_t kMaxPacket = PacketRing<WhenFull::Refuse, kCapacity>::kMaxPacket;

	// no bytes
	TcpClientData() = default;

	// how long the oldest closed packet has waited, in ms; 0 while none waits
	inline uint32_t waitedMs() const { return waited; }

	// bytes add() takes now
	size_t spaceLeft() const;

	// a closed packet waits
	bool hasPacket() const;

	// no bytes wait, neither closed packets nor an open one
	bool empty() const;

	// Adds size bytes from the socket to the open packet; false, nothing added, when they do not fit. The silence starts again.
	bool add(const uint8_t* data, size_t size);

	// Counts dtMs of silence and of waiting, then closes the open packet after coalesceMs of silence (0: at once), when it holds
	// chunkBytes (0: no such limit) or when the buffer is full. The update after add() counts no silence.
	void update(uint32_t dtMs, uint16_t coalesceMs, size_t chunkBytes);

	// The oldest closed packet's bytes from offset from on, at most room of them, in out; returns its full size, 0 when none waits.
	size_t peek(uint8_t* out, size_t room, size_t from) const;

	// The oldest closed packet was written or dropped; the next one waits from now.
	void pop();

private:
	PacketRing<WhenFull::Refuse, kCapacity> ring;

	uint32_t silenceMs = 0;

	// bytes came since the last update()
	bool bytesAdded = false;

	uint32_t waited = 0;
};

}  // namespace w2b
