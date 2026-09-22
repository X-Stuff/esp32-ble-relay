#include "WifiSupervisor.h"

#include <iterator>

#include "Logger.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "wifi";

// The strongest access point other than the link's; null when the scan found no other.
const AccessPoint* strongestOther(const std::vector<AccessPoint>& found, const BSSID& current)
{
	const AccessPoint* best = nullptr;

	for (const AccessPoint& point : found)
	{
		if (point.bssid() != current && (best == nullptr || point.rssi() > best->rssi()))
		{
			best = &point;
		}
	}

	return best;
}

}  // namespace

WifiSupervisor::WifiSupervisor(WifiRadio& radio, const Config& config, LedIndicator& led)
	: radio(radio), config(config), led(led)
{
}

const char* WifiSupervisor::nameOf(JoinResult result)
{
	switch (result)
	{
		case JoinResult::None:             return "none";
		case JoinResult::Joining:          return "joining";
		case JoinResult::Connected:        return "connected";
		case JoinResult::PasswordRejected: return "password_rejected";
		case JoinResult::NetworkNotFound:  return "network_not_found";
		case JoinResult::NoLink:           return "no_link";
	}

	return "?";
}

void WifiSupervisor::setup()
{
	if (configured())
	{
		enterConnecting(kFallbackMs);
	}
	else
	{
		enterPortal();
	}
}

void WifiSupervisor::update(uint32_t dtMs)
{
	switch (current)
	{
		case State::Portal:
		{
			updatePortal(dtMs);
			break;
		}
		case State::Connecting:
		{
			updateConnecting(dtMs);
			break;
		}
		case State::Connected:
		{
			updateConnected(dtMs);
			break;
		}
		case State::Handover:
		{
			updateHandover(dtMs);
			break;
		}
	}
}

void WifiSupervisor::reconnect()
{
	current = State::Handover;
	stateMs = 0;

	Logger::info(kTag, "new settings saved: joining \"%s\" in %u s", config.wifi().ssid().c_str(), (unsigned)(kHandoverMs / 1000));
}

bool WifiSupervisor::configured() const
{
	return !config.wifi().ssid().empty();
}

// Connecting after a Connect, until the link comes up or the portal opens again.
bool WifiSupervisor::joining() const
{
	return join == JoinResult::Joining;
}

// An attempt in progress goes on beside the portal.
void WifiSupervisor::enterPortal()
{
	current = State::Portal;
	clients = 0;

	radio.openPortal();
	led.set(LedPattern::Blink1Hz);

	if (configured())
	{
		Logger::info(kTag, "setup portal open; the station retries while no client is on it");
	}
	else
	{
		Logger::info(kTag, "no WiFi settings: setup portal open");
	}
}

void WifiSupervisor::enterConnecting(uint32_t noLinkLimitMs)
{
	current = State::Connecting;
	stateMs = 0;
	limitMs = noLinkLimitMs;
	backoffStep = 0;

	radio.closePortal();
	led.set(LedPattern::Blink5Hz);

	Logger::info(kTag, "connecting to \"%s\"; the setup portal opens after %u s without a link", config.wifi().ssid().c_str(),
		(unsigned)(limitMs / 1000));

	startAttempt();
}

void WifiSupervisor::enterConnected()
{
	current = State::Connected;
	attempting = false;
	backoffStep = 0;
	clients = 0;

	if (join != JoinResult::None)
	{
		join = JoinResult::Connected;
	}

	resetRoaming();

	radio.closePortal();
	led.set(LedPattern::Heartbeat3s);

	Logger::info(kTag, "connected to \"%s\": %s", config.wifi().ssid().c_str(), radio.addressSummary().c_str());
	Logger::info(kTag, "link: %s", radio.radioSummary().c_str());
}

void WifiSupervisor::updatePortal(uint32_t dtMs)
{
	if (radio.linkUp())
	{
		enterConnected();
		return;
	}

	watchClients();

	if (configured() && clients == 0)
	{
		runRetries(dtMs);
	}
}

void WifiSupervisor::updateConnecting(uint32_t dtMs)
{
	if (radio.linkUp())
	{
		enterConnected();
		return;
	}

	if (joining() && endJoinOnRefusal())
	{
		return;
	}

	stateMs += dtMs;

	if (stateMs >= limitMs)
	{
		if (joining())
		{
			join = JoinResult::NoLink;
		}

		Logger::warn(kTag, "no link to \"%s\" within %u s", config.wifi().ssid().c_str(), (unsigned)(limitMs / 1000));
		enterPortal();
		return;
	}

	runRetries(dtMs);
}

void WifiSupervisor::updateConnected(uint32_t dtMs)
{
	if (!radio.linkUp())
	{
		Logger::warn(kTag, "link to \"%s\" lost", config.wifi().ssid().c_str());
		enterConnecting(kFallbackMs);
		return;
	}

	const std::optional<int8_t> sample = sampleSignal(dtMs);

	if (scanning)
	{
		collectRoamScan(dtMs);
		return;
	}

	if (restMs > 0)
	{
		restMs = restMs > dtMs ? restMs - dtMs : 0;
		return;
	}

	watchSignal(dtMs, sample);
}

std::optional<int8_t> WifiSupervisor::sampleSignal(uint32_t dtMs)
{
	sampleMs += dtMs;

	if (sampleMs < kSignalSampleMs)
	{
		return std::nullopt;
	}

	sampleMs = 0;

	return radio.linkRssi();
}

// A mean over the window, so single dips and peaks do not decide.
void WifiSupervisor::watchSignal(uint32_t dtMs, const std::optional<int8_t>& sample)
{
	windowMs += dtMs;

	if (sample)
	{
		signalSum += *sample;
		++samples;
	}

	if (windowMs < kSignalWindowMs)
	{
		return;
	}

	const bool sampled = samples > 0;
	const int32_t mean = sampled ? signalSum / static_cast<int32_t>(samples) : 0;
	resetWindow();

	if (sampled && mean < kRoamBelowDbm)
	{
		startRoamScan(static_cast<int8_t>(mean));
	}
}

// Another scan holding the driver (the setup page's) leaves this one for the next window.
void WifiSupervisor::startRoamScan(int8_t meanRssi)
{
	const WifiConfig wifi = config.wifi();

	if (!radio.startScan(wifi.ssid().c_str()))
	{
		Logger::debug(kTag, "signal %d dBm, but the roaming scan did not start; next look in %u s", meanRssi, (unsigned)(kSignalWindowMs / 1000));
		return;
	}

	scanning = true;
	scanMs = 0;
	weakRssi = meanRssi;

	Logger::info(kTag, "signal %d dBm on average over %u s, below %d dBm: looking for a stronger access point of \"%s\"", meanRssi,
		(unsigned)(kSignalWindowMs / 1000), kRoamBelowDbm, wifi.ssid().c_str());
}

void WifiSupervisor::collectRoamScan(uint32_t dtMs)
{
	std::vector<AccessPoint> found;
	const ScanStatus status = radio.scanResult(found);
	scanMs += dtMs;

	if (status == ScanStatus::Running && scanMs < kRoamScanLimitMs)
	{
		return;
	}

	scanning = false;
	restMs = kRoamRestMs;

	const unsigned restMin = kRoamRestMs / 60000;

	if (status != ScanStatus::Done)
	{
		Logger::info(kTag, "the roaming scan failed; next look in %u min", restMin);
		return;
	}

	const AccessPoint* best = strongestOther(found, radio.linkBssid());

	if (best == nullptr)
	{
		Logger::info(kTag, "no other access point of \"%s\" found; next look in %u min", config.wifi().ssid().c_str(), restMin);
		return;
	}

	if (best->rssi() < weakRssi + kRoamMarginDb)
	{
		Logger::info(kTag, "no access point of \"%s\" at least %d dB above %d dBm (best other %s, %d dBm); next look in %u min",
			config.wifi().ssid().c_str(), kRoamMarginDb, weakRssi, BSSIDStr(best->bssid()).c_str(), best->rssi(), restMin);
		return;
	}

	roamTo(*best);
}

void WifiSupervisor::roamTo(const AccessPoint& target)
{
	Logger::info(kTag, "roaming to %s on channel %u, %d dBm (link %d dBm)", BSSIDStr(target.bssid()).c_str(), (unsigned)target.channel(),
		target.rssi(), weakRssi);

	roamTarget = target;
	radio.disconnect();
	enterConnecting(kFallbackMs);
}

// A fresh link starts a fresh window.
void WifiSupervisor::resetRoaming()
{
	resetWindow();
	scanning = false;
	scanMs = 0;
	restMs = 0;
}

void WifiSupervisor::resetWindow()
{
	windowMs = 0;
	sampleMs = 0;
	signalSum = 0;
	samples = 0;
}

// The old link or attempt ends only now, so a Connect from the LAN page gets its answer over it too.
void WifiSupervisor::updateHandover(uint32_t dtMs)
{
	stateMs += dtMs;

	if (stateMs < kHandoverMs)
	{
		return;
	}

	radio.disconnect();
	attempting = false;
	join = JoinResult::Joining;
	enterConnecting(kJoinLimitMs);
}

// The settings were just typed in: retrying will not change a refusal, so the portal comes back for a correction.
bool WifiSupervisor::endJoinOnRefusal()
{
	const JoinFailure failure = radio.joinFailure();

	if (failure != JoinFailure::PasswordRejected && failure != JoinFailure::NetworkNotFound)
	{
		return false;
	}

	join = failure == JoinFailure::PasswordRejected ? JoinResult::PasswordRejected : JoinResult::NetworkNotFound;
	Logger::warn(kTag, "Connect to \"%s\" failed: %s", config.wifi().ssid().c_str(), nameOf(join));

	if (attempting)
	{
		endAttempt();
	}

	enterPortal();

	return true;
}

// A client arriving ends the attempt in progress: joining moves the radio, and the access point with it, to the network's channel.
void WifiSupervisor::watchClients()
{
	const uint8_t count = radio.portalClients();

	if (count == clients)
	{
		return;
	}

	clients = count;

	if (clients > 0 && attempting)
	{
		endAttempt();
	}

	if (!configured())
	{
		Logger::info(kTag, "%u client(s) on the setup portal", (unsigned)clients);
	}
	else if (clients > 0)
	{
		Logger::info(kTag, "%u client(s) on the setup portal: station retries paused", (unsigned)clients);
	}
	else
	{
		Logger::info(kTag, "no client on the setup portal: station retries resume in %u s", (unsigned)(waitMs / 1000));
	}
}

void WifiSupervisor::runRetries(uint32_t dtMs)
{
	if (attempting)
	{
		attemptMs += dtMs;

		if (attemptMs >= kAttemptMs)
		{
			endAttempt();
			Logger::info(kTag, "no link after %u s; next attempt in %u s", (unsigned)(kAttemptMs / 1000), (unsigned)(waitMs / 1000));
		}

		return;
	}

	waitMs = waitMs > dtMs ? waitMs - dtMs : 0;

	if (waitMs == 0)
	{
		startAttempt();
	}
}

// The first attempt after a roaming decision goes to the chosen access point; later ones, if it fails, to the strongest.
void WifiSupervisor::startAttempt()
{
	const WifiConfig wifi = config.wifi();
	const DeviceConfig device = config.device();

	attempting = true;
	attemptMs = 0;

	if (roamTarget)
	{
		radio.connectTo(wifi, device.hostname().c_str(), *roamTarget);
		roamTarget.reset();
		return;
	}

	radio.connect(wifi, device.hostname().c_str());
}

void WifiSupervisor::endAttempt()
{
	radio.disconnect();
	attempting = false;
	waitMs = nextWaitMs();
}

uint32_t WifiSupervisor::nextWaitMs()
{
	const size_t last = std::size(kBackoffMs) - 1;
	const uint32_t wait = kBackoffMs[backoffStep];

	backoffStep = backoffStep < last ? backoffStep + 1 : last;

	return wait;
}

}  // namespace w2b
