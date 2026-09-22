#include "AccessPoint.h"

namespace w2b
{

AccessPoint::AccessPoint(const BSSID& bssid, int8_t rssi, uint8_t channel)
	: mac(bssid), signal(rssi), radioChannel(channel)
{
}

}  // namespace w2b
