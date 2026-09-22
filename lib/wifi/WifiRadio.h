#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "AccessPoint.h"
#include "WifiConfig.h"
#include "WifiLink.h"

namespace w2b
{

// Why the driver says the last attempt failed, grouped as the supervisor needs it
enum class JoinFailure : uint8_t
{
	// no failure reported since connect()
	None,

	// the network refused the password, or the handshake timed out, which a wrong password causes
	PasswordRejected,

	// no access point with the name, or none with a security mode the station accepts
	NetworkNotFound,

	// anything else, such as a lost beacon; retried as usual
	Other,
};

// Where a scan for roaming is
enum class ScanStatus : uint8_t
{
	// the driver scans
	Running,

	// the access points found are in the result
	Done,

	// it ended without a result, or another scan took the driver
	Failed,
};

// The WiFi radio as WifiSupervisor drives it: the setup portal's access point, the station and the roaming scans. Every call returns at
// once; an attempt shows up in linkUp(), a scan in scanResult().
class WifiRadio
{
public:
	virtual ~WifiRadio() = default;

	// AP + STA: the open access point and its DNS catch-all come up; a station attempt in progress goes on
	virtual void openPortal() = 0;

	// station only: the access point goes down and drops its clients; the station link stays
	virtual void closePortal() = 0;

	// clients associated with the access point; 0 while it is down
	virtual uint8_t portalClients() const = 0;

	// One attempt to join wifi (its static address, or DHCP) as hostname, at the strongest access point with its name.
	virtual void connect(const WifiConfig& wifi, const char* hostname) = 0;

	// connect() to one access point of the network: its BSSID and channel
	virtual void connectTo(const WifiConfig& wifi, const char* hostname, const AccessPoint& target) = 0;

	// ends the attempt in progress or the link
	virtual void disconnect() = 0;

	// associated and holding an address
	virtual bool linkUp() const = 0;

	// the failure the driver reported for the current attempt; connect() resets it to None
	virtual JoinFailure joinFailure() const = 0;

	// reads the signal of the link from the driver, in dBm, and keeps it as link()'s; meaningful while linkUp()
	virtual int8_t linkRssi() = 0;

	// access point of the link; meaningful while linkUp()
	virtual BSSID linkBssid() const = 0;

	// the link as the driver last reported it: the join's facts, the addresses and the last linkRssi(); no driver call
	virtual WifiLink link() const = 0;

	// Starts a scan for the access points named ssid, the link staying up; false when one runs already or it did not start.
	virtual bool startScan(const char* ssid) = 0;

	// Running until the scan has ended, then Done with the access points found in found, or Failed; the result is handed over once.
	virtual ScanStatus scanResult(std::vector<AccessPoint>& found) = 0;

	// for the log: address, mask, gateway and DNS of the link
	virtual std::string addressSummary() const = 0;

	// for the log: signal, channel, BSSID and hostname of the link
	virtual std::string radioSummary() const = 0;
};

}  // namespace w2b
