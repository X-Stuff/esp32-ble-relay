#include "WifiLink.h"

#include "IPv4Str.h"
#include "JsonMembers.h"

namespace w2b
{

void WifiLink::setNetwork(const char* ssid, const BSSID& bssid, uint8_t channel)
{
	network.assign(ssid);
	accessPoint = bssid;
	channelNumber = channel;
}

void WifiLink::setAddresses(uint32_t address, uint32_t mask, uint32_t gateway, uint32_t dns)
{
	ip = address;
	netmask = mask;
	gw = gateway;
	nameServer = dns;
	connected = true;
}

void WifiLink::setRssi(int8_t rssi)
{
	signal = rssi;
}

void WifiLink::setHostname(const char* hostname)
{
	name.assign(hostname);
}

void WifiLink::clear()
{
	connected = false;
	network = WifiConfig::SSID();
	accessPoint = BSSID{};
	channelNumber = 0;
	signal = 0;
	ip = 0;
	netmask = 0;
	gw = 0;
	nameServer = 0;
}

void WifiLink::toJson(JsonObject& json) const
{
	json["ssid"] = network.c_str();
	json["connected"] = connected;
	json["address"] = IPv4Str(ip).c_str();
	json["mask"] = IPv4Str(netmask).c_str();
	json["gateway"] = IPv4Str(gw).c_str();
	json["dns"] = IPv4Str(nameServer).c_str();
	json["rssi"] = signal;
	json["channel"] = channelNumber;
	json["bssid"] = connected ? BSSIDStr(accessPoint).c_str() : "";
}

}  // namespace w2b
