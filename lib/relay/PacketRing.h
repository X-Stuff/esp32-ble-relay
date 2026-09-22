// A ring buffer of variable-length packets, oldest first: each is stored as a 2-byte length and its bytes, which may wrap around the
// end. WhenFull says what a packet that does not fit does. Not thread-safe: the owner locks.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace w2b
{

// What a PacketRing does with bytes that do not fit
enum class WhenFull : uint8_t
{
	// drops the oldest packets whole, read or not, until they fit
	DropOldest,

	// refuses them; space comes back only with pop()
	Refuse,
};

// Packets in Capacity bytes. Readers each keep their own place, and pop() takes the oldest packet out. An open packet at the end
// grows with append() and is seen by readers only after close().
template <WhenFull Policy, size_t Capacity>
class PacketRing
{
public:
	// bytes of a packet's length, before its bytes
	static constexpr size_t kLengthBytes = 2;

	// the longest packet
	static constexpr size_t kMaxPacket = std::min<size_t>(Capacity - kLengthBytes, 0xffff);

	static_assert(Capacity > kLengthBytes, "a packet needs more than its length");

	// A reader's place in the ring: the next packet it reads, and the bytes of it read so far
	class Reader
	{
	public:
		// packets dropped from the ring before this reader read them all
		inline uint32_t lost() const { return lostCount; }

	private:
		friend class PacketRing;

		uint32_t sequence = 0;

		size_t offset = 0;

		// bytes of the next packet read by skip()
		size_t done = 0;

		uint32_t lostCount = 0;
	};

	// empty
	PacketRing() = default;

	// bytes in use, the lengths and the open packet included
	inline size_t size() const { return used; }

	// no closed packet
	inline bool isEmpty() const { return firstSequence == nextSequence; }

	// bytes the open packet holds; 0 when none is open
	inline size_t openSize() const { return openBytes; }

	// packets refused: empty, longer than kMaxPacket, pushed while a packet is open, or, with Refuse, not fitting
	inline uint32_t refused() const { return refusedCount; }

	// packets dropped to make room, with DropOldest
	inline uint32_t dropped() const { return droppedCount; }

	// A reader that starts at the next packet closed after this call.
	Reader createReader() const
	{
		Reader reader;
		reader.sequence = nextSequence;
		reader.offset = isOpen ? openStart : wrap(head + used);

		return reader;
	}

	// Adds size bytes as one closed packet; false, and counted, when refused (see refused()). DropOldest drops old packets for room.
	bool push(const uint8_t* data, size_t size)
	{
		if (data == nullptr || size == 0 || size > kMaxPacket || isOpen || !makeRoom(kLengthBytes + size))
		{
			++refusedCount;
			return false;
		}

		const size_t start = wrap(head + used);
		writeLength(start, size);
		writeBytes(wrap(start + kLengthBytes), data, size);
		used += kLengthBytes + size;
		++nextSequence;

		return true;
	}

	// Bytes append() takes now; with DropOldest the closed packets it may drop for room count as free.
	size_t spaceLeft() const
	{
		const size_t reserve = isOpen ? 0 : kLengthBytes;
		const size_t droppable = Policy == WhenFull::DropOldest ? closedBytes() : 0;
		const size_t free = Capacity - used + droppable;

		return free > reserve ? std::min(free - reserve, kMaxPacket - openBytes) : 0;
	}

	// Adds size bytes to the open packet, opening one when none is; false, nothing added, when they do not fit (spaceLeft()).
	bool append(const uint8_t* data, size_t size)
	{
		if (data == nullptr || size == 0)
		{
			return size == 0;
		}

		if (size > spaceLeft() || !makeRoom((isOpen ? 0 : kLengthBytes) + size))
		{
			return false;
		}

		if (!isOpen)
		{
			openStart = wrap(head + used);
			used += kLengthBytes;
			isOpen = true;
		}

		writeBytes(wrap(openStart + kLengthBytes + openBytes), data, size);
		openBytes += size;
		used += size;

		return true;
	}

	// Closes the open packet, so readers and pop() see it; an open packet with no bytes goes. Nothing when none is open.
	void close()
	{
		if (!isOpen)
		{
			return;
		}

		if (openBytes == 0)
		{
			used -= kLengthBytes;
		}
		else
		{
			writeLength(openStart, openBytes);
			++nextSequence;
		}

		isOpen = false;
		openBytes = 0;
	}

	// Copies what is left of reader's next packet to out, at most room bytes, and moves the reader past it; returns how many bytes were
	// left, the packet's full size unless skip() took part of it; 0 when it read them all.
	size_t read(Reader& reader, uint8_t* out, size_t room) const
	{
		const size_t left = peek(reader, out, room);
		skip(reader, left);

		return left;
	}

	// Copies what is left of reader's next packet to out, at most room bytes, without moving the reader; returns how many bytes are
	// left, 0 when it read them all. A reader behind the oldest packet moves there first and counts the packets it lost.
	size_t peek(Reader& reader, uint8_t* out, size_t room) const
	{
		catchUp(reader);

		if (!isBefore(reader.sequence, nextSequence))
		{
			return 0;
		}

		const size_t left = readLength(reader.offset) - reader.done;
		readBytes(wrap(reader.offset + kLengthBytes + reader.done), out, out != nullptr ? std::min(left, room) : 0);

		return left;
	}

	// Moves reader on by count bytes of its next packet, past it once they are all read. A reader whose packet was dropped since
	// peek() moves to the oldest instead and counts the packets it lost.
	void skip(Reader& reader, size_t count) const
	{
		if (catchUp(reader) || !isBefore(reader.sequence, nextSequence))
		{
			return;
		}

		const size_t size = readLength(reader.offset);

		if (reader.done + count < size)
		{
			reader.done += count;
			return;
		}

		reader.offset = wrap(reader.offset + kLengthBytes + size);
		reader.done = 0;
		++reader.sequence;
	}

	// Copies the oldest closed packet's bytes from offset from on to out, at most room of them; returns its full size, 0 when empty.
	size_t peek(uint8_t* out, size_t room, size_t from) const
	{
		if (isEmpty())
		{
			return 0;
		}

		const size_t size = readLength(head);
		const size_t start = std::min(from, size);
		readBytes(wrap(head + kLengthBytes + start), out, out != nullptr ? std::min(size - start, room) : 0);

		return size;
	}

	// Takes the oldest closed packet out; readers that had not read it count it as lost. Nothing when empty.
	void pop()
	{
		if (!isEmpty())
		{
			dropOldest();
		}
	}

private:
	// a comes before b, sequence numbers wrapping around
	static bool isBefore(uint32_t a, uint32_t b)
	{
		return static_cast<int32_t>(a - b) < 0;
	}

	static size_t wrap(size_t offset)
	{
		return offset % Capacity;
	}

	// a reader behind the oldest packet moves to its start and counts the packets it lost; true when it moved
	bool catchUp(Reader& reader) const
	{
		if (!isBefore(reader.sequence, firstSequence))
		{
			return false;
		}

		reader.lostCount += firstSequence - reader.sequence;
		reader.sequence = firstSequence;
		reader.offset = head;
		reader.done = 0;

		return true;
	}

	// the bytes of the closed packets, their lengths included
	size_t closedBytes() const
	{
		return isOpen ? wrap(openStart + Capacity - head) : used;
	}

	// DropOldest drops closed packets until needed bytes are free; true when they are
	bool makeRoom(size_t needed)
	{
		while (Policy == WhenFull::DropOldest && Capacity - used < needed && !isEmpty())
		{
			dropOldest();
			++droppedCount;
		}

		return Capacity - used >= needed;
	}

	void dropOldest()
	{
		const size_t size = kLengthBytes + readLength(head);
		head = wrap(head + size);
		used -= size;
		++firstSequence;
	}

	void writeLength(size_t offset, size_t length)
	{
		const uint8_t bytes[kLengthBytes] = {static_cast<uint8_t>(length & 0xff), static_cast<uint8_t>(length >> 8)};
		writeBytes(offset, bytes, kLengthBytes);
	}

	size_t readLength(size_t offset) const
	{
		uint8_t bytes[kLengthBytes];
		readBytes(offset, bytes, kLengthBytes);

		return bytes[0] | (bytes[1] << 8);
	}

	// copies size bytes to offset, the part past the end to the start
	void writeBytes(size_t offset, const uint8_t* data, size_t size)
	{
		if (size == 0)
		{
			return;
		}

		const size_t first = std::min(size, Capacity - offset);
		std::memcpy(storage.data() + offset, data, first);
		std::memcpy(storage.data(), data + first, size - first);
	}

	void readBytes(size_t offset, uint8_t* out, size_t size) const
	{
		if (size == 0)
		{
			return;
		}

		const size_t first = std::min(size, Capacity - offset);
		std::memcpy(out, storage.data() + offset, first);
		std::memcpy(out + first, storage.data(), size - first);
	}

	std::array<uint8_t, Capacity> storage{};

	// where the oldest packet starts
	size_t head = 0;

	size_t used = 0;

	// the sequence number of the oldest closed packet, and of the next one to close
	uint32_t firstSequence = 0;

	uint32_t nextSequence = 0;

	bool isOpen = false;

	// where the open packet's length goes
	size_t openStart = 0;

	size_t openBytes = 0;

	uint32_t refusedCount = 0;

	uint32_t droppedCount = 0;
};

}  // namespace w2b
