#include "Esp32WifiRadio.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <WiFi.h>

#include "Logger.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "radio";
constexpr uint16_t kDnsPort = 53;

// A host-order address, 192.168.0.1 as 0xc0a80001.
IPAddress addressOf(uint32_t address)
{
	return IPAddress(address >> 24, address >> 16 & 0xff, address >> 8 & 0xff, address & 0xff);
}

// The host-order value of an address the WiFi library holds.
uint32_t hostOrderOf(const IPAddress& address)
{
	return (uint32_t)address[0] << 24 | (uint32_t)address[1] << 16 | (uint32_t)address[2] << 8 | address[3];
}

// ESP-IDF disconnect reasons; a wrong WPA2 password shows as a handshake timeout on most access points.
JoinFailure failureOf(uint16_t reason)
{
	switch (reason)
	{
		case 0:
			return JoinFailure::None;

		case WIFI_REASON_AUTH_FAIL:
		case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
		case WIFI_REASON_HANDSHAKE_TIMEOUT:
			return JoinFailure::PasswordRejected;

		case WIFI_REASON_NO_AP_FOUND:
		case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
		case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
			return JoinFailure::NetworkNotFound;

		default:
			return JoinFailure::Other;
	}
}

}  // namespace

Esp32WifiRadio::Esp32WifiRadio(const char* apSSID)
	: apSSID(apSSID)
{
}

// AP + STA keeps the station as it is.
void Esp32WifiRadio::openPortal()
{
	prepare();

	if (!WiFi.mode(WIFI_AP_STA) || !WiFi.softAP(apSSID, nullptr))
	{
		Logger::error(kTag, "starting the access point \"%s\" failed", apSSID);
		return;
	}

	IPAddress address = WiFi.softAPIP();

	if (!dns.start(kDnsPort, "*", address))
	{
		Logger::warn(kTag, "the DNS server did not start: no captive portal, the page still answers at http://%s/", address.toString().c_str());
	}

	Logger::info(kTag, "open access point \"%s\" up, setup page http://%s/, DNS answers every name with it", apSSID, address.toString().c_str());
}

void Esp32WifiRadio::closePortal()
{
	prepare();

	const bool wasOpen = (WiFi.getMode() & WIFI_AP) != 0;

	dns.stop();

	if (!WiFi.mode(WIFI_STA))
	{
		Logger::error(kTag, "switching to station only failed");
		return;
	}

	if (wasOpen)
	{
		Logger::info(kTag, "access point \"%s\" down", apSSID);
	}
}

uint8_t Esp32WifiRadio::portalClients() const
{
	return WiFi.softAPgetStationNum();
}

void Esp32WifiRadio::connect(const WifiConfig& wifi, const char* hostname)
{
	join(wifi, hostname, nullptr);
}

void Esp32WifiRadio::connectTo(const WifiConfig& wifi, const char* hostname, const AccessPoint& target)
{
	join(wifi, hostname, &target);
}

// No wait: the supervisor starts the next attempt only after its backoff.
void Esp32WifiRadio::disconnect()
{
	const bool eraseAp = false;
	const unsigned long timeoutMs = 0;

	WiFi.STA.disconnect(eraseAp, timeoutMs);
}

bool Esp32WifiRadio::linkUp() const
{
	return WiFi.status() == WL_CONNECTED;
}

JoinFailure Esp32WifiRadio::joinFailure() const
{
	return failureOf(lastReason);
}

int8_t Esp32WifiRadio::linkRssi()
{
	const int8_t rssi = WiFi.RSSI();

	LockGuard guard(&lock);
	current.setRssi(rssi);

	return rssi;
}

WifiLink Esp32WifiRadio::link() const
{
	LockGuard guard(&lock);

	return current;
}

BSSID Esp32WifiRadio::linkBssid() const
{
	BSSID bssid{};
	WiFi.BSSID(bssid.data());

	return bssid;
}

// Directed probes for the one name, so a hidden network answers too.
bool Esp32WifiRadio::startScan(const char* ssid)
{
	if (WiFi.scanComplete() == WIFI_SCAN_RUNNING)
	{
		return false;
	}

	const bool async = true;
	const bool showHidden = true;
	const bool passive = false;
	const uint8_t allChannels = 0;

	scanSSID.assign(ssid);

	return WiFi.scanNetworks(async, showHidden, passive, kScanMsPerChannel, allChannels, scanSSID.c_str()) == WIFI_SCAN_RUNNING;
}

// Only the scanned name counts: a scan the setup page started meanwhile returns every network it found.
ScanStatus Esp32WifiRadio::scanResult(std::vector<AccessPoint>& found)
{
	const int16_t count = WiFi.scanComplete();

	if (count == WIFI_SCAN_RUNNING)
	{
		return ScanStatus::Running;
	}

	if (count < 0)
	{
		return ScanStatus::Failed;
	}

	found.clear();

	for (int16_t i = 0; i < count; ++i)
	{
		if (WiFi.SSID(i) != scanSSID.c_str())
		{
			continue;
		}

		BSSID bssid{};
		WiFi.BSSID(i, bssid.data());
		found.push_back(AccessPoint(bssid, static_cast<int8_t>(WiFi.RSSI(i)), static_cast<uint8_t>(WiFi.channel(i))));
	}

	WiFi.scanDelete();

	return ScanStatus::Done;
}

std::string Esp32WifiRadio::addressSummary() const
{
	char text[Logger::kMaxLine];
	snprintf(text, sizeof(text), "address %s, mask %s, gateway %s, dns %s", WiFi.localIP().toString().c_str(), WiFi.subnetMask().toString().c_str(),
		WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP().toString().c_str());

	return text;
}

std::string Esp32WifiRadio::radioSummary() const
{
	const char* hostname = WiFi.STA.getHostname();

	char text[Logger::kMaxLine];
	snprintf(text, sizeof(text), "rssi %d dBm, channel %d, bssid %s, hostname %s", (int)WiFi.RSSI(), (int)WiFi.channel(), WiFi.BSSIDstr().c_str(),
		hostname != nullptr ? hostname : "?");

	return text;
}

void Esp32WifiRadio::prepare()
{
	WiFi.persistent(false);
	WiFi.setAutoReconnect(false);

	// WIFI_FAST_SCAN, the core's default, joins the first access point with the name from channel 1 on, with a mesh maybe a far node.
	WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
	WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

	if (listening)
	{
		return;
	}

	WiFi.onEvent(
		[this](arduino_event_id_t id, arduino_event_info_t info)
		{
			handleEvent(id, info);
		});

	listening = true;
}

// ASSOC_LEAVE, our own disconnect(), is not kept, so it cannot hide the reason a link ended.
void Esp32WifiRadio::handleEvent(arduino_event_id_t id, const arduino_event_info_t& info)
{
	switch (id)
	{
		case ARDUINO_EVENT_WIFI_STA_CONNECTED:
		{
			const wifi_event_sta_connected_t& joined = info.wifi_sta_connected;
			const size_t length = std::min<size_t>(joined.ssid_len, WifiConfig::SSID::kCapacity);
			char ssid[WifiConfig::SSID::kCapacity + 1];
			memcpy(ssid, joined.ssid, length);
			ssid[length] = '\0';
			BSSID bssid{};
			memcpy(bssid.data(), joined.bssid, bssid.size());

			LockGuard guard(&lock);
			current.setNetwork(ssid, bssid, joined.channel);
			break;
		}

		case ARDUINO_EVENT_WIFI_STA_GOT_IP:
		{
			const esp_netif_ip_info_t& addresses = info.got_ip.ip_info;
			const uint32_t dns = hostOrderOf(WiFi.dnsIP());

			LockGuard guard(&lock);
			current.setAddresses(hostOrderOf(IPAddress(addresses.ip.addr)), hostOrderOf(IPAddress(addresses.netmask.addr)),
				hostOrderOf(IPAddress(addresses.gw.addr)), dns);
			break;
		}

		case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
		{
			const uint16_t reason = info.wifi_sta_disconnected.reason;

			if (reason != WIFI_REASON_ASSOC_LEAVE)
			{
				lastReason = reason;
			}

			LockGuard guard(&lock);
			current.clear();
			break;
		}

		case ARDUINO_EVENT_WIFI_STA_LOST_IP:
		{
			LockGuard guard(&lock);
			current.clear();
			break;
		}

		default:
		{
			break;
		}
	}
}

// A target fixes BSSID and channel; the core drops a link that is still up first (up to 1 s).
void Esp32WifiRadio::join(const WifiConfig& wifi, const char* hostname, const AccessPoint* target)
{
	prepare();

	if (!configureStation(wifi, hostname))
	{
		Logger::error(kTag, "preparing the station for \"%s\" failed", wifi.ssid().c_str());
		return;
	}

	const char* password = wifi.password().empty() ? nullptr : wifi.password().c_str();
	const int32_t anyChannel = 0;
	const int32_t channel = target != nullptr ? target->channel() : anyChannel;
	const uint8_t* bssid = target != nullptr ? target->bssid().data() : nullptr;
	lastReason = 0;

	{
		LockGuard guard(&lock);
		current.setHostname(hostname);
	}

	if (!WiFi.STA.connect(wifi.ssid().c_str(), password, channel, bssid))
	{
		Logger::error(kTag, "starting to join \"%s\" failed", wifi.ssid().c_str());
		return;
	}

	Logger::debug(kTag, "joining \"%s\" as %s%s%s", wifi.ssid().c_str(), hostname, target != nullptr ? " at " : "",
		target != nullptr ? BSSIDStr(target->bssid()).c_str() : "");
}

// WiFi.STA.config, not WiFi.config, which swaps its arguments when the mask does not start with 255; no arguments is DHCP.
bool Esp32WifiRadio::configureStation(const WifiConfig& wifi, const char* hostname)
{
	if (!WiFi.STA.begin() || !WiFi.STA.setHostname(hostname))
	{
		return false;
	}

	if (!wifi.staticIp())
	{
		return WiFi.STA.config();
	}

	const StaticIpConfig& address = *wifi.staticIp();
	const IPAddress dns = address.dns() != 0 ? addressOf(address.dns()) : IPAddress();

	return WiFi.STA.config(addressOf(address.ip()), addressOf(address.gateway()), addressOf(address.subnet()), dns);
}

}  // namespace w2b
