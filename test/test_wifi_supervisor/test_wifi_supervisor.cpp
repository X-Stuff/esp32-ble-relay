#include <unity.h>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "Config.h"
#include "DigitalOut.h"
#include "LedIndicator.h"
#include "Lock.h"
#include "LogRing.h"
#include "LogStamp.h"
#include "Logger.h"
#include "WifiRadio.h"
#include "WifiSupervisor.h"

using w2b::AccessPoint;
using w2b::BSSID;
using w2b::Config;
using w2b::ConfigData;
using w2b::DeviceConfig;
using w2b::LedPattern;
using w2b::LogLevel;
using w2b::WifiConfig;
using w2b::WifiSupervisor;
using State = w2b::WifiSupervisor::State;
using JoinResult = w2b::WifiSupervisor::JoinResult;

namespace
{

class FakePin : public w2b::DigitalOut
{
public:
	void setup() override
	{
	}

	void write(bool) override
	{
	}
};

class FakeRadio : public w2b::WifiRadio
{
public:
	void openPortal() override
	{
		portalOpen = true;
	}

	void closePortal() override
	{
		portalOpen = false;
		clients = 0;
	}

	uint8_t portalClients() const override
	{
		return portalOpen ? clients : 0;
	}

	void connect(const WifiConfig& wifi, const char* hostname) override
	{
		++attempts;
		ssid = wifi.ssid().c_str();
		host = hostname;
		failure = w2b::JoinFailure::None;
		target.reset();
	}

	void connectTo(const WifiConfig& wifi, const char* hostname, const AccessPoint& chosen) override
	{
		connect(wifi, hostname);
		target = chosen;
	}

	void disconnect() override
	{
		++disconnects;
		linked = false;
	}

	bool linkUp() const override
	{
		return linked;
	}

	w2b::JoinFailure joinFailure() const override
	{
		return failure;
	}

	int8_t linkRssi() override
	{
		return rssi;
	}

	BSSID linkBssid() const override
	{
		return bssid;
	}

	w2b::WifiLink link() const override
	{
		return w2b::WifiLink();
	}

	bool startScan(const char* name) override
	{
		if (scanBusy)
		{
			return false;
		}

		++scans;
		scanSSID = name;
		scan = w2b::ScanStatus::Running;

		return true;
	}

	w2b::ScanStatus scanResult(std::vector<AccessPoint>& found) override
	{
		if (scan == w2b::ScanStatus::Done)
		{
			found = heard;
		}

		return scan;
	}

	std::string addressSummary() const override
	{
		return "address 192.168.0.50";
	}

	std::string radioSummary() const override
	{
		return "rssi -49 dBm";
	}

	bool portalOpen = false;

	uint8_t clients = 0;

	int attempts = 0;

	int disconnects = 0;

	bool linked = false;

	w2b::JoinFailure failure = w2b::JoinFailure::None;

	std::string ssid;

	std::string host;

	// the access point of the latest attempt, when connectTo() chose one
	std::optional<AccessPoint> target;

	int8_t rssi = -49;

	BSSID bssid = {0x02, 0, 0, 0, 0, 0x01};

	// roaming scans: started, refused while busy, their state and what they heard
	int scans = 0;

	bool scanBusy = false;

	std::string scanSSID;

	w2b::ScanStatus scan = w2b::ScanStatus::Failed;

	std::vector<AccessPoint> heard;
};

const BSSID kOtherNode = {0x02, 0, 0, 0, 0, 0x02};
const BSSID kThirdNode = {0x02, 0, 0, 0, 0, 0x03};

constexpr uint32_t kTickMs = 100;

size_t zeroStamp(char* out, size_t cap)
{
	return w2b::formatUptimeStamp(0, out, cap);
}

char logStorage[8192];
w2b::LogRing logRing(logStorage, sizeof(logStorage));
w2b::Logger logger(logRing, zeroStamp, nullptr);

struct FakeLock : w2b::Lock
{
	void lock() override
	{
	}

	void unlock() override
	{
	}
};

bool acceptSave(const ConfigData&)
{
	return true;
}

FakePin pin;
w2b::LedIndicator led(pin, true);
FakeRadio radio;
FakeLock lock;
Config config(lock, acceptSave);
uint32_t nowMs = 0;

ConfigData homeConfig()
{
	return ConfigData(DeviceConfig("relay-1", LogLevel::Info, w2b::defaultTimeZone(), false, DeviceConfig::kDefaultHeartbeatS,
		DeviceConfig::kUncalibratedBatteryScalePpm), WifiConfig("Home", "correct horse", std::nullopt), {});
}

// data becomes the running configuration, as a load at boot or a saved change does.
void use(const ConfigData& data)
{
	config.load([&data](ConfigData& running)
		{
			running = data;
		});
}

// Updates every kTickMs until nowMs reaches untilMs.
void runUntil(WifiSupervisor& wifi, uint32_t untilMs)
{
	while (nowMs < untilMs)
	{
		nowMs += kTickMs;
		wifi.update(kTickMs);
	}
}

bool logged(const char* part)
{
	char text[8192];
	size_t n = logRing.copyTo(text, sizeof(text) - 1);
	text[n] = '\0';

	return std::strstr(text, part) != nullptr;
}

}  // namespace

void setUp()
{
	radio = FakeRadio();
	use(ConfigData());
	nowMs = 0;
	logRing.clear();
	led.set(LedPattern::Off);
}

void tearDown()
{
}

static void test_without_settings_the_portal_opens_and_nothing_is_tried()
{
	WifiSupervisor wifi(radio, config, led);
	wifi.setup();

	TEST_ASSERT_EQUAL(static_cast<int>(State::Portal), static_cast<int>(wifi.state()));
	TEST_ASSERT_TRUE(radio.portalOpen);
	TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::Blink1Hz), static_cast<int>(led.pattern()));

	runUntil(wifi, 300000);
	TEST_ASSERT_EQUAL(0, radio.attempts);
	TEST_ASSERT_TRUE(radio.portalOpen);
}

static void test_with_settings_the_station_tries_at_once()
{
	use(homeConfig());
	WifiSupervisor wifi(radio, config, led);
	wifi.setup();

	TEST_ASSERT_EQUAL(static_cast<int>(State::Connecting), static_cast<int>(wifi.state()));
	TEST_ASSERT_FALSE(radio.portalOpen);
	TEST_ASSERT_EQUAL(1, radio.attempts);
	TEST_ASSERT_EQUAL_STRING("Home", radio.ssid.c_str());
	TEST_ASSERT_EQUAL_STRING("relay-1", radio.host.c_str());
	TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::Blink5Hz), static_cast<int>(led.pattern()));
}

static void test_a_link_means_connected()
{
	use(homeConfig());
	WifiSupervisor wifi(radio, config, led);
	wifi.setup();

	radio.linked =true;
	runUntil(wifi, 100);

	TEST_ASSERT_EQUAL(static_cast<int>(State::Connected), static_cast<int>(wifi.state()));
	TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::Heartbeat3s), static_cast<int>(led.pattern()));
	TEST_ASSERT_TRUE(logged("connected to \"Home\": address 192.168.0.50"));
	TEST_ASSERT_TRUE(logged("link: rssi -49 dBm"));

	runUntil(wifi, 300000);
	TEST_ASSERT_EQUAL(1, radio.attempts);
}

// Attempts of 15 s, waits of 5, 10, 20, 30 and then 60 s; the portal opens 60 s after the start, the retries go on beside it.
static void test_attempts_back_off_and_the_portal_opens_after_60_s()
{
	use(homeConfig());
	WifiSupervisor wifi(radio, config, led);
	wifi.setup();

	runUntil(wifi, 14900);
	TEST_ASSERT_EQUAL(0, radio.disconnects);
	runUntil(wifi, 15100);
	TEST_ASSERT_EQUAL(1, radio.disconnects);

	runUntil(wifi, 19900);
	TEST_ASSERT_EQUAL(1, radio.attempts);
	runUntil(wifi, 20100);
	TEST_ASSERT_EQUAL(2, radio.attempts);

	runUntil(wifi, 44900);
	TEST_ASSERT_EQUAL(2, radio.attempts);
	runUntil(wifi, 45100);
	TEST_ASSERT_EQUAL(3, radio.attempts);

	runUntil(wifi, 59900);
	TEST_ASSERT_FALSE(radio.portalOpen);
	runUntil(wifi, 60100);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Portal), static_cast<int>(wifi.state()));
	TEST_ASSERT_TRUE(radio.portalOpen);
	TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::Blink1Hz), static_cast<int>(led.pattern()));

	// attempt 3 ends at 60.1 s, then 20 s, 30 s, 60 s, 60 s
	runUntil(wifi, 80000);
	TEST_ASSERT_EQUAL(3, radio.attempts);
	runUntil(wifi, 80300);
	TEST_ASSERT_EQUAL(4, radio.attempts);

	runUntil(wifi, 125000);
	TEST_ASSERT_EQUAL(4, radio.attempts);
	runUntil(wifi, 125300);
	TEST_ASSERT_EQUAL(5, radio.attempts);

	runUntil(wifi, 200000);
	TEST_ASSERT_EQUAL(5, radio.attempts);
	runUntil(wifi, 200300);
	TEST_ASSERT_EQUAL(6, radio.attempts);

	runUntil(wifi, 275000);
	TEST_ASSERT_EQUAL(6, radio.attempts);
	runUntil(wifi, 275300);
	TEST_ASSERT_EQUAL(7, radio.attempts);
}

static void test_a_client_on_the_portal_ends_the_attempt_and_pauses_retries()
{
	use(homeConfig());
	WifiSupervisor wifi(radio, config, led);
	wifi.setup();

	// in the portal, attempt 4 runs from 80.1 s
	runUntil(wifi, 81000);
	TEST_ASSERT_EQUAL(4, radio.attempts);
	int disconnects = radio.disconnects;

	radio.clients = 1;
	runUntil(wifi, 81100);
	TEST_ASSERT_EQUAL(disconnects + 1, radio.disconnects);
	TEST_ASSERT_TRUE(logged("1 client(s) on the setup portal: station retries paused"));

	runUntil(wifi, 600000);
	TEST_ASSERT_EQUAL(4, radio.attempts);
	TEST_ASSERT_TRUE(radio.portalOpen);

	// the wait after attempt 4 is 30 s and starts counting when the last client leaves
	radio.clients = 0;
	runUntil(wifi, 629900);
	TEST_ASSERT_EQUAL(4, radio.attempts);
	runUntil(wifi, 630200);
	TEST_ASSERT_EQUAL(5, radio.attempts);
}

static void test_a_link_during_the_portal_closes_it()
{
	use(homeConfig());
	WifiSupervisor wifi(radio, config, led);
	wifi.setup();

	runUntil(wifi, 61000);
	TEST_ASSERT_TRUE(radio.portalOpen);

	radio.linked =true;
	runUntil(wifi, 61100);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Connected), static_cast<int>(wifi.state()));
	TEST_ASSERT_FALSE(radio.portalOpen);
}

static void test_a_lost_link_starts_over_with_the_60_s_fallback()
{
	use(homeConfig());
	WifiSupervisor wifi(radio, config, led);
	wifi.setup();
	radio.linked =true;
	runUntil(wifi, 1000);

	radio.linked =false;
	runUntil(wifi, 1100);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Connecting), static_cast<int>(wifi.state()));
	TEST_ASSERT_EQUAL(2, radio.attempts);
	TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::Blink5Hz), static_cast<int>(led.pattern()));
	TEST_ASSERT_TRUE(logged("link to \"Home\" lost"));

	runUntil(wifi, 61000);
	TEST_ASSERT_FALSE(radio.portalOpen);
	runUntil(wifi, 61200);
	TEST_ASSERT_TRUE(radio.portalOpen);
}

// Connect: the portal stays 2 s so the page reads "saved", then the station tries alone; no link within 30 s brings the portal back.
static void test_connect_hands_over_then_falls_back_after_30_s()
{
	WifiSupervisor wifi(radio, config, led);
	wifi.setup();
	radio.clients = 1;
	runUntil(wifi, 5000);

	use(homeConfig());
	wifi.reconnect();
	TEST_ASSERT_EQUAL(static_cast<int>(State::Handover), static_cast<int>(wifi.state()));

	runUntil(wifi, 6900);
	TEST_ASSERT_TRUE(radio.portalOpen);
	TEST_ASSERT_EQUAL(0, radio.attempts);

	runUntil(wifi, 7100);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Connecting), static_cast<int>(wifi.state()));
	TEST_ASSERT_FALSE(radio.portalOpen);
	TEST_ASSERT_EQUAL(1, radio.attempts);
	TEST_ASSERT_EQUAL_STRING("Home", radio.ssid.c_str());

	runUntil(wifi, 36900);
	TEST_ASSERT_FALSE(radio.portalOpen);
	TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::Joining), static_cast<int>(wifi.lastJoin()));
	runUntil(wifi, 37200);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Portal), static_cast<int>(wifi.state()));
	TEST_ASSERT_TRUE(radio.portalOpen);
	TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::NoLink), static_cast<int>(wifi.lastJoin()));
}

// Fail fast: in the join after a Connect, a refusal reported by the driver brings the portal back at once.
static void test_after_a_connect_a_refusal_reopens_the_portal_at_once()
{
	WifiSupervisor wifi(radio, config, led);
	wifi.setup();

	use(homeConfig());
	wifi.reconnect();
	runUntil(wifi, 2100);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Connecting), static_cast<int>(wifi.state()));
	TEST_ASSERT_EQUAL(1, radio.attempts);

	radio.failure = w2b::JoinFailure::PasswordRejected;
	runUntil(wifi, 2200);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Portal), static_cast<int>(wifi.state()));
	TEST_ASSERT_TRUE(radio.portalOpen);
	TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::PasswordRejected), static_cast<int>(wifi.lastJoin()));
	TEST_ASSERT_EQUAL_STRING("password_rejected", WifiSupervisor::nameOf(wifi.lastJoin()));
	TEST_ASSERT_TRUE(logged("Connect to \"Home\" failed: password_rejected"));

	// the attempt was ended; the retries go on beside the portal after the first wait
	TEST_ASSERT_EQUAL(2, radio.disconnects);
	runUntil(wifi, 7100);
	TEST_ASSERT_EQUAL(1, radio.attempts);
	runUntil(wifi, 7300);
	TEST_ASSERT_EQUAL(2, radio.attempts);

	wifi.reconnect();
	runUntil(wifi, 9400);
	radio.failure = w2b::JoinFailure::NetworkNotFound;
	runUntil(wifi, 9500);
	TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::NetworkNotFound), static_cast<int>(wifi.lastJoin()));
	TEST_ASSERT_TRUE(radio.portalOpen);
}

// After a lost link or at boot a refusal may be a weak signal: the retries and the 60 s fallback go on as usual.
static void test_the_normal_retries_ignore_a_refusal()
{
	use(homeConfig());
	WifiSupervisor wifi(radio, config, led);
	wifi.setup();

	radio.failure = w2b::JoinFailure::PasswordRejected;
	runUntil(wifi, 59900);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Connecting), static_cast<int>(wifi.state()));
	TEST_ASSERT_FALSE(radio.portalOpen);
	TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::None), static_cast<int>(wifi.lastJoin()));
}

// Only a refusal ends a join early; any other failure waits for the 30 s limit.
static void test_other_failures_do_not_end_a_join()
{
	WifiSupervisor wifi(radio, config, led);
	wifi.setup();

	use(homeConfig());
	wifi.reconnect();
	runUntil(wifi, 2100);

	radio.failure = w2b::JoinFailure::Other;
	runUntil(wifi, 31900);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Connecting), static_cast<int>(wifi.state()));
	TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::Joining), static_cast<int>(wifi.lastJoin()));

	runUntil(wifi, 32100);
	TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::NoLink), static_cast<int>(wifi.lastJoin()));
}

// A Connect from the LAN page: the old link carries the answer, then it ends and the new settings are tried.
static void test_connect_while_connected_keeps_the_link_for_the_handover()
{
	use(homeConfig());
	WifiSupervisor wifi(radio, config, led);
	wifi.setup();
	radio.linked =true;
	runUntil(wifi, 1000);

	use(ConfigData(config.device(), WifiConfig("Office", "another secret", std::nullopt), {}));
	wifi.reconnect();

	runUntil(wifi, 2900);
	TEST_ASSERT_EQUAL(0, radio.disconnects);
	TEST_ASSERT_TRUE(radio.linked);

	runUntil(wifi, 3100);
	TEST_ASSERT_EQUAL(1, radio.disconnects);
	TEST_ASSERT_EQUAL(2, radio.attempts);
	TEST_ASSERT_EQUAL_STRING("Office", radio.ssid.c_str());

	radio.linked =true;
	runUntil(wifi, 3200);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Connected), static_cast<int>(wifi.state()));
	TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::Connected), static_cast<int>(wifi.lastJoin()));
}

// Roaming. The link comes up at 100 ms; the first signal window ends 30 s later, at 30.1 s.
static void connectAt100Ms(WifiSupervisor& wifi)
{
	use(homeConfig());
	wifi.setup();
	radio.linked =true;
	runUntil(wifi, 100);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Connected), static_cast<int>(wifi.state()));
}

static void test_a_strong_link_never_scans()
{
	WifiSupervisor wifi(radio, config, led);
	connectAt100Ms(wifi);

	radio.rssi = -70;
	runUntil(wifi, 600000);

	TEST_ASSERT_EQUAL(0, radio.scans);
	TEST_ASSERT_EQUAL(1, radio.attempts);
}

static void test_a_weak_mean_over_the_window_starts_a_scan()
{
	WifiSupervisor wifi(radio, config, led);
	connectAt100Ms(wifi);

	radio.rssi = -80;
	runUntil(wifi, 30000);
	TEST_ASSERT_EQUAL(0, radio.scans);

	runUntil(wifi, 30200);
	TEST_ASSERT_EQUAL(1, radio.scans);
	TEST_ASSERT_EQUAL_STRING("Home", radio.scanSSID.c_str());
	TEST_ASSERT_TRUE(logged("signal -80 dBm on average over 30 s, below -70 dBm"));
	TEST_ASSERT_TRUE(radio.linked);
}

// Samples of -60 and -90 alternate: a mean of -75, although half of them are strong.
static void test_the_mean_decides_not_single_samples()
{
	WifiSupervisor wifi(radio, config, led);
	connectAt100Ms(wifi);

	// the sample at k s + 100 ms reads the value set at k s: 15 of each over the window
	while (nowMs < 30100)
	{
		radio.rssi = (nowMs / 1000) % 2 == 0 ? -60 : -90;
		runUntil(wifi, nowMs + kTickMs);
	}

	runUntil(wifi, 30200);
	TEST_ASSERT_EQUAL(1, radio.scans);
	TEST_ASSERT_TRUE(logged("signal -75 dBm on average"));
}

static void test_a_stronger_access_point_is_joined_by_its_bssid()
{
	WifiSupervisor wifi(radio, config, led);
	connectAt100Ms(wifi);

	radio.rssi = -80;
	runUntil(wifi, 30200);

	radio.heard = {AccessPoint(radio.bssid, -79, 1), AccessPoint(kThirdNode, -65, 11), AccessPoint(kOtherNode, -55, 6)};
	radio.scan = w2b::ScanStatus::Done;
	runUntil(wifi, 30300);

	TEST_ASSERT_TRUE(logged("roaming to 02:00:00:00:00:02 on channel 6, -55 dBm (link -80 dBm)"));
	TEST_ASSERT_EQUAL(1, radio.disconnects);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Connecting), static_cast<int>(wifi.state()));
	TEST_ASSERT_EQUAL(2, radio.attempts);
	TEST_ASSERT_TRUE(radio.target.has_value());
	TEST_ASSERT_TRUE(radio.target->bssid() == kOtherNode);
	TEST_ASSERT_EQUAL_UINT8(6, radio.target->channel());

	radio.linked =true;
	runUntil(wifi, 30400);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Connected), static_cast<int>(wifi.state()));
}

// 5 dB is not enough: the link stays, and the next look comes 5 min after the scan plus a window.
static void test_less_than_the_margin_keeps_the_link_and_rests()
{
	WifiSupervisor wifi(radio, config, led);
	connectAt100Ms(wifi);

	radio.rssi = -80;
	runUntil(wifi, 30200);

	radio.heard = {AccessPoint(radio.bssid, -80, 1), AccessPoint(kOtherNode, -75, 6)};
	radio.scan = w2b::ScanStatus::Done;
	runUntil(wifi, 30300);

	TEST_ASSERT_EQUAL(0, radio.disconnects);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Connected), static_cast<int>(wifi.state()));
	TEST_ASSERT_TRUE(logged("no access point of \"Home\" at least 8 dB above -80 dBm (best other 02:00:00:00:00:02, -75 dBm); next look in 5 min"));

	runUntil(wifi, 360000);
	TEST_ASSERT_EQUAL(1, radio.scans);
	runUntil(wifi, 360500);
	TEST_ASSERT_EQUAL(2, radio.scans);
}

static void test_only_the_own_access_point_heard_keeps_the_link()
{
	WifiSupervisor wifi(radio, config, led);
	connectAt100Ms(wifi);

	radio.rssi = -85;
	runUntil(wifi, 30200);

	radio.heard = {AccessPoint(radio.bssid, -60, 1)};
	radio.scan = w2b::ScanStatus::Done;
	runUntil(wifi, 30300);

	TEST_ASSERT_EQUAL(0, radio.disconnects);
	TEST_ASSERT_TRUE(logged("no other access point of \"Home\" found; next look in 5 min"));
}

// The setup page's scan holds the driver: the roaming scan waits for the next window.
static void test_a_busy_scanner_waits_for_the_next_window()
{
	WifiSupervisor wifi(radio, config, led);
	connectAt100Ms(wifi);

	radio.rssi = -80;
	radio.scanBusy = true;
	runUntil(wifi, 30200);
	TEST_ASSERT_EQUAL(0, radio.scans);

	radio.scanBusy = false;
	runUntil(wifi, 60000);
	TEST_ASSERT_EQUAL(0, radio.scans);
	runUntil(wifi, 60200);
	TEST_ASSERT_EQUAL(1, radio.scans);
}

static void test_a_scan_that_never_ends_counts_as_failed()
{
	WifiSupervisor wifi(radio, config, led);
	connectAt100Ms(wifi);

	radio.rssi = -80;
	runUntil(wifi, 30200);
	TEST_ASSERT_EQUAL(static_cast<int>(w2b::ScanStatus::Running), static_cast<int>(radio.scan));

	runUntil(wifi, 45000);
	TEST_ASSERT_FALSE(logged("the roaming scan failed"));
	runUntil(wifi, 45300);
	TEST_ASSERT_TRUE(logged("the roaming scan failed; next look in 5 min"));
	TEST_ASSERT_EQUAL(0, radio.disconnects);
}

// A failed join at the chosen access point: the next attempts go to the strongest one, as usual.
static void test_after_a_failed_roam_the_retries_join_as_usual()
{
	WifiSupervisor wifi(radio, config, led);
	connectAt100Ms(wifi);

	radio.rssi = -80;
	runUntil(wifi, 30200);
	radio.heard = {AccessPoint(kOtherNode, -50, 6)};
	radio.scan = w2b::ScanStatus::Done;
	runUntil(wifi, 30300);
	TEST_ASSERT_TRUE(radio.target.has_value());

	// attempt of 15 s, then 5 s wait
	runUntil(wifi, 50400);
	TEST_ASSERT_EQUAL(3, radio.attempts);
	TEST_ASSERT_FALSE(radio.target.has_value());
}

static void test_a_link_lost_during_a_scan_reconnects_as_usual()
{
	WifiSupervisor wifi(radio, config, led);
	connectAt100Ms(wifi);

	radio.rssi = -80;
	runUntil(wifi, 30200);
	radio.linked =false;
	runUntil(wifi, 30300);

	TEST_ASSERT_EQUAL(static_cast<int>(State::Connecting), static_cast<int>(wifi.state()));
	TEST_ASSERT_EQUAL(2, radio.attempts);
	TEST_ASSERT_FALSE(radio.target.has_value());
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_without_settings_the_portal_opens_and_nothing_is_tried);
	RUN_TEST(test_with_settings_the_station_tries_at_once);
	RUN_TEST(test_a_link_means_connected);
	RUN_TEST(test_attempts_back_off_and_the_portal_opens_after_60_s);
	RUN_TEST(test_a_client_on_the_portal_ends_the_attempt_and_pauses_retries);
	RUN_TEST(test_a_link_during_the_portal_closes_it);
	RUN_TEST(test_a_lost_link_starts_over_with_the_60_s_fallback);
	RUN_TEST(test_connect_hands_over_then_falls_back_after_30_s);
	RUN_TEST(test_after_a_connect_a_refusal_reopens_the_portal_at_once);
	RUN_TEST(test_the_normal_retries_ignore_a_refusal);
	RUN_TEST(test_other_failures_do_not_end_a_join);
	RUN_TEST(test_connect_while_connected_keeps_the_link_for_the_handover);
	RUN_TEST(test_a_strong_link_never_scans);
	RUN_TEST(test_a_weak_mean_over_the_window_starts_a_scan);
	RUN_TEST(test_the_mean_decides_not_single_samples);
	RUN_TEST(test_a_stronger_access_point_is_joined_by_its_bssid);
	RUN_TEST(test_less_than_the_margin_keeps_the_link_and_rests);
	RUN_TEST(test_only_the_own_access_point_heard_keeps_the_link);
	RUN_TEST(test_a_busy_scanner_waits_for_the_next_window);
	RUN_TEST(test_a_scan_that_never_ends_counts_as_failed);
	RUN_TEST(test_after_a_failed_roam_the_retries_join_as_usual);
	RUN_TEST(test_a_link_lost_during_a_scan_reconnects_as_usual);

	return UNITY_END();
}
