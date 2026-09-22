#include "WriteMode.h"

namespace w2b
{

namespace
{

constexpr size_t kAttHeaderBytes = 3;

}  // namespace

size_t maxWriteBytes(uint16_t mtu)
{
	return mtu > kAttHeaderBytes ? mtu - kAttHeaderBytes : 0;
}

WriteMode chooseWriteMode(size_t size, uint16_t mtu, RuleConfig::Oversize oversize)
{
	if (size <= maxWriteBytes(mtu))
	{
		return WriteMode::Single;
	}

	switch (oversize)
	{
		case RuleConfig::Oversize::Chunk:     return WriteMode::Chunks;
		case RuleConfig::Oversize::LongWrite: return size <= kMaxAttributeValue ? WriteMode::LongWrite : WriteMode::Drop;
		case RuleConfig::Oversize::Drop:      return WriteMode::Drop;
	}

	return WriteMode::Drop;
}

}  // namespace w2b
