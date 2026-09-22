// How a TCP client's packet goes to a rule's TX characteristic: the writes it takes at the link's MTU and the rule's oversize policy.
#pragma once

#include <cstddef>
#include <cstdint>

#include "RuleConfig.h"

namespace w2b
{

// the longest ATT value; a long write takes no more
constexpr size_t kMaxAttributeValue = 512;

// How a packet is written to TX
enum class WriteMode : uint8_t
{
	// one write
	Single,

	// consecutive writes of maxWriteBytes() each, the last shorter
	Chunks,

	// one ATT prepare/execute write, one value at the device
	LongWrite,

	// none: the packet is dropped and counted
	Drop,
};

// the most bytes one write carries at mtu: the ATT header takes 3
size_t maxWriteBytes(uint16_t mtu);

// Chooses how a packet of size bytes is written: one write up to maxWriteBytes(mtu), else as oversize says: chunks, a long write up
// to kMaxAttributeValue (dropped beyond), or drop.
WriteMode chooseWriteMode(size_t size, uint16_t mtu, RuleConfig::Oversize oversize);

}  // namespace w2b
