#include "RelayStatus.h"

#include <algorithm>

#include <ArduinoJson.h>

namespace w2b
{

const char* linkStateStr(LinkState state)
{
	switch (state)
	{
		case LinkState::Disabled:      return "disabled";
		case LinkState::Idle:          return "idle";
		case LinkState::Searching:     return "searching";
		case LinkState::Connecting:    return "connecting";
		case LinkState::Discovering:   return "discovering";
		case LinkState::Subscribing:   return "subscribing";
		case LinkState::Up:            return "up";
		case LinkState::Waiting:       return "waiting";
		case LinkState::Disconnecting: return "disconnecting";
		case LinkState::Error:         return "error";
	}

	return "?";
}

void RelayCounters::countRx(size_t bytes)
{
	++rxFrameCount;
	rxByteCount += bytes;
}

void RelayCounters::countRxDropped(uint32_t frames)
{
	rxDropCount += frames;
}

void RelayCounters::countTcpRx(size_t bytes)
{
	tcpRxByteCount += bytes;
}

void RelayCounters::countTcpTx(size_t bytes)
{
	tcpTxByteCount += bytes;
}

void RelayCounters::countTxWrite()
{
	++txWriteCount;
}

void RelayCounters::countTxDropped(size_t bytes)
{
	txDropCount += bytes;
}

void RelayCounters::countOversize()
{
	++oversizeCount;
}

void RelayCounters::countReconnect()
{
	++reconnectCount;
}

void RelayCounters::toJson(JsonObject& json) const
{
	json["rx_frames"] = rxFrameCount;
	json["rx_bytes"] = rxByteCount;
	json["rx_dropped"] = rxDropCount;
	json["tcp_rx_bytes"] = tcpRxByteCount;
	json["tcp_tx_bytes"] = tcpTxByteCount;
	json["ble_tx_writes"] = txWriteCount;
	json["tx_dropped"] = txDropCount;
	json["oversize_events"] = oversizeCount;
	json["reconnects"] = reconnectCount;
}

TcpStatus::TcpStatus(bool listening, const Error& error, const Clients& clients, size_t clientCount)
	: accepting(listening), problem(error), peers(clients), peerCount(std::min(clientCount, clients.size()))
{
}

void TcpStatus::toJson(JsonObject& json) const
{
	json["listening"] = accepting;
	json["tcp_error"] = problem.c_str();
	JsonArray list = json["clients"].to<JsonArray>();

	for (size_t i = 0; i < peerCount; ++i)
	{
		list.add(peers[i].c_str());
	}
}

RelayStatus::RelayStatus(const RuleConfig::Id& id, const RuleConfig::Name& name, LinkState state, const std::optional<BleAddress>& address,
	uint16_t mtu, int8_t rssi, uint32_t upS, const Error& error, const RelayCounters& counters, const TcpStatus& tcp)
	: ruleId(id), ruleName(name), linkState(state), deviceAddress(address), attMtu(mtu), rssiDbm(rssi), upTimeS(upS), errorText(error),
		traffic(counters), server(tcp)
{
}

void RelayStatus::toJson(JsonObject& json) const
{
	json["id"] = ruleId.c_str();
	json["name"] = ruleName.c_str();
	json["state"] = linkStateStr(linkState);
	json["address"] = deviceAddress ? deviceAddress->text().c_str() : "";
	json["mtu"] = attMtu;
	json["rssi"] = rssiDbm;
	json["up_s"] = upTimeS;
	json["error"] = errorText.c_str();
	traffic.toJson(json);
	server.toJson(json);
}

}  // namespace w2b
