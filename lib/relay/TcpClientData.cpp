#include "TcpClientData.h"

namespace w2b
{

size_t TcpClientData::spaceLeft() const
{
	return ring.spaceLeft();
}

bool TcpClientData::empty() const
{
	return ring.size() == 0;
}

bool TcpClientData::hasPacket() const
{
	return !ring.isEmpty();
}

bool TcpClientData::add(const uint8_t* data, size_t size)
{
	if (!ring.append(data, size))
	{
		return false;
	}

	silenceMs = 0;
	bytesAdded = size > 0;

	return true;
}

void TcpClientData::update(uint32_t dtMs, uint16_t coalesceMs, size_t chunkBytes)
{
	if (hasPacket())
	{
		waited += dtMs;
	}

	if (ring.openSize() == 0)
	{
		return;
	}

	silenceMs += bytesAdded ? 0 : dtMs;
	bytesAdded = false;

	const bool quiet = silenceMs >= coalesceMs;
	const bool chunk = chunkBytes > 0 && ring.openSize() >= chunkBytes;

	if (quiet || chunk || ring.spaceLeft() == 0)
	{
		ring.close();
	}
}

size_t TcpClientData::peek(uint8_t* out, size_t room, size_t from) const
{
	return ring.peek(out, room, from);
}

void TcpClientData::pop()
{
	ring.pop();
	waited = 0;
}

}  // namespace w2b
