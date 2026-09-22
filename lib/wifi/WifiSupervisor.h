#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "Component.h"
#include "Config.h"
#include "LedIndicator.h"
#include "WifiRadio.h"

namespace w2b
{

// Keeps the station on the saved network, falls back to the setup portal without a link, and roams to a stronger access point while
// the signal is weak. update(dtMs) drives every timer; application thread only, except lastJoin().
class WifiSupervisor : public Component
{
public:
	// how long one attempt to join lasts
	static constexpr uint32_t kAttemptMs = 15000;

	// Connecting without a link this long opens the portal
	static constexpr uint32_t kFallbackMs = 60000;

	// the join after a Connect without a link this long opens the portal
	static constexpr uint32_t kJoinLimitMs = 30000;

	// after a Connect everything stays as it is this long, so the page reads the result
	static constexpr uint32_t kHandoverMs = 2000;

	// waits between attempts; the last one repeats
	static constexpr uint32_t kBackoffMs[] = {5000, 10000, 20000, 30000, 60000};

	// a mean signal below this starts a roaming scan
	static constexpr int8_t kRoamBelowDbm = -70;

	// an access point at least this much stronger than the link is joined
	static constexpr int8_t kRoamMarginDb = 8;

	// the signal is sampled this often while connected
	static constexpr uint32_t kSignalSampleMs = 1000;

	// the mean signal is taken over this window
	static constexpr uint32_t kSignalWindowMs = 30000;

	// after a roaming scan the signal is watched again this much later
	static constexpr uint32_t kRoamRestMs = 300000;

	// a scan that has not ended by then counts as failed
	static constexpr uint32_t kRoamScanLimitMs = 15000;

	// What the station is doing
	enum class State : uint8_t
	{
		// AP + STA; with saved settings the station retries while no client is on the access point
		Portal,

		// station only, retrying with backoff; the portal opens after the limit
		Connecting,

		// the station holds an address; the signal is watched for roaming
		Connected,

		// a Connect was saved; everything stays for kHandoverMs, then Connecting
		Handover,
	};

	// How the last Connect ended
	enum class JoinResult : uint8_t
	{
		// no Connect since boot
		None,

		// its join runs
		Joining,

		// it joined
		Connected,

		// the network refused the password
		PasswordRejected,

		// no access point of the network was found
		NetworkNotFound,

		// no link within kJoinLimitMs, for no reason the driver named
		NoLink,
	};

	// config: the WiFi settings and the hostname, read for every attempt; led: shows the state
	WifiSupervisor(WifiRadio& radio, const Config& config, LedIndicator& led);

	// what the station is doing
	inline State state() const { return current; }

	// how the last Connect ended; any thread
	inline JoinResult lastJoin() const { return join; }

	// "none", "joining", "connected", "password_rejected", "network_not_found", "no_link"
	static const char* nameOf(JoinResult result);

	// saved settings: Connecting; none: Portal
	void setup() override;

	// steps the state and its timers by dtMs
	void update(uint32_t dtMs) override;

	// New settings are in config: Handover, then a join that fails fast on a refusal.
	void reconnect();

private:
	bool configured() const;

	bool joining() const;

	void enterPortal();

	void enterConnecting(uint32_t noLinkLimitMs);

	void enterConnected();

	void updatePortal(uint32_t dtMs);

	void updateConnecting(uint32_t dtMs);

	void updateConnected(uint32_t dtMs);

	void updateHandover(uint32_t dtMs);

	// the signal every kSignalSampleMs while connected, so the radio's link() stays fresh; the sample taken now, else none
	std::optional<int8_t> sampleSignal(uint32_t dtMs);

	// the window over the samples; a weak mean starts a roaming scan
	void watchSignal(uint32_t dtMs, const std::optional<int8_t>& sample);

	void startRoamScan(int8_t meanRssi);

	void collectRoamScan(uint32_t dtMs);

	void roamTo(const AccessPoint& target);

	void resetRoaming();

	void resetWindow();

	bool endJoinOnRefusal();

	void watchClients();

	void runRetries(uint32_t dtMs);

	void startAttempt();

	void endAttempt();

	uint32_t nextWaitMs();

	WifiRadio& radio;

	const Config& config;

	LedIndicator& led;

	State current = State::Portal;

	// time spent in Connecting or Handover
	uint32_t stateMs = 0;

	// Connecting: the portal opens when stateMs reaches this
	uint32_t limitMs = kFallbackMs;

	bool attempting = false;

	// time into the attempt in progress
	uint32_t attemptMs = 0;

	// between attempts: the wait left before the next one
	uint32_t waitMs = 0;

	// index into kBackoffMs for the next wait
	size_t backoffStep = 0;

	// clients on the access point at the last look; Portal only
	uint8_t clients = 0;

	// Connected: time into the signal window
	uint32_t windowMs = 0;

	// Connected: time since the window's last sample
	uint32_t sampleMs = 0;

	// Connected: the sum of the window's samples
	int32_t signalSum = 0;

	// Connected: the window's samples
	uint32_t samples = 0;

	// Connected: a roaming scan runs
	bool scanning = false;

	// Connected: time into the roaming scan
	uint32_t scanMs = 0;

	// Connected: the mean signal that started the roaming scan
	int8_t weakRssi = 0;

	// Connected: time left before the signal is watched again
	uint32_t restMs = 0;

	// the access point the next attempt joins, set by roaming
	std::optional<AccessPoint> roamTarget;

	// written on the application thread, read by the web server's handlers
	std::atomic<JoinResult> join{JoinResult::None};
};

}  // namespace w2b
