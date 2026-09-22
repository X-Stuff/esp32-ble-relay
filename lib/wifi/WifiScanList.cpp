#include "WifiScanList.h"

#include <algorithm>

#include <ArduinoJson.h>

namespace w2b
{

WifiNetwork::WifiNetwork(const WifiConfig::SSID& ssid, int32_t rssi, int32_t channel, bool secured)
	: name(ssid), signal(rssi), radioChannel(channel), encrypted(secured)
{
}

void WifiNetwork::toJson(JsonObject& json) const
{
	json["ssid"] = name.c_str();
	json["rssi"] = signal;
	json["channel"] = radioChannel;
	json["secured"] = encrypted;
}

void WifiScanList::add(const WifiNetwork& network)
{
	if (network.ssid().empty())
	{
		return;
	}

	auto same = std::find_if(found.begin(), found.end(),
		[&network](const WifiNetwork& known)
		{
			return known.ssid() == network.ssid();
		});

	if (same != found.end())
	{
		if (same->rssi() >= network.rssi())
		{
			return;
		}

		found.erase(same);
	}

	// After every entry at least as strong, so equal signals keep the scan's order.
	auto weaker = std::find_if(found.begin(), found.end(),
		[&network](const WifiNetwork& known)
		{
			return known.rssi() < network.rssi();
		});

	found.insert(weaker, network);

	if (found.size() > kMaxNetworks)
	{
		found.pop_back();
	}
}

void WifiScanList::toJson(JsonObject& json) const
{
	JsonArray list = json["networks"].to<JsonArray>();

	for (const WifiNetwork& network : found)
	{
		JsonObject item = list.add<JsonObject>();
		network.toJson(item);
	}
}

}  // namespace w2b
