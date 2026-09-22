#include <unity.h>

#include <string>

#include <ArduinoJson.h>

#include "Bssid.h"
#include "WifiLink.h"
#include "WifiScanList.h"

using w2b::WifiNetwork;
using w2b::WifiScanList;

namespace
{

WifiNetwork net(const char* ssid, int32_t rssi)
{
	return WifiNetwork(ssid, rssi, 6, true);
}

std::string ssids(const WifiScanList& list)
{
	std::string text;

	for (const WifiNetwork& network : list.networks())
	{
		text += std::string(network.ssid().c_str()) + " ";
	}

	return text;
}

}  // namespace

void setUp()
{
}

void tearDown()
{
}

static void test_hidden_networks_are_left_out()
{
	WifiScanList list;
	list.add(net("", -40));
	list.add(net("home", -60));

	TEST_ASSERT_EQUAL_STRING("home ", ssids(list).c_str());
}

static void test_strongest_first_equal_signals_keep_scan_order()
{
	WifiScanList list;
	list.add(net("b", -70));
	list.add(net("a", -50));
	list.add(net("c", -70));
	list.add(net("d", -90));

	TEST_ASSERT_EQUAL_STRING("a b c d ", ssids(list).c_str());
}

static void test_one_entry_per_ssid_with_the_stronger_signal()
{
	WifiScanList list;
	list.add(net("mesh", -80));
	list.add(net("other", -60));
	list.add(net("mesh", -50));
	list.add(net("mesh", -90));

	TEST_ASSERT_EQUAL_STRING("mesh other ", ssids(list).c_str());
	TEST_ASSERT_EQUAL(-50, list.networks()[0].rssi());
}

static void test_a_full_list_drops_the_weakest()
{
	WifiScanList list;

	for (size_t i = 0; i < WifiScanList::kMaxNetworks; ++i)
	{
		list.add(net(("n" + std::to_string(i)).c_str(), -50 - static_cast<int32_t>(i)));
	}

	list.add(net("weakest", -99));
	TEST_ASSERT_EQUAL(WifiScanList::kMaxNetworks, list.networks().size());
	TEST_ASSERT_TRUE(ssids(list).find("weakest") == std::string::npos);

	list.add(net("strong", -10));
	TEST_ASSERT_EQUAL(WifiScanList::kMaxNetworks, list.networks().size());
	TEST_ASSERT_EQUAL_STRING("strong", list.networks().front().ssid().c_str());
	TEST_ASSERT_EQUAL_STRING("n18", list.networks().back().ssid().c_str());
}

static void test_json_layout()
{
	WifiScanList list;
	list.add(WifiNetwork("cafe", -71, 11, false));
	list.add(WifiNetwork("home", -48, 1, true));

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	list.toJson(root);

	std::string text;
	serializeJson(doc, text);

	TEST_ASSERT_EQUAL_STRING("{\"networks\":[{\"ssid\":\"home\",\"rssi\":-48,\"channel\":1,\"secured\":true},"
		"{\"ssid\":\"cafe\",\"rssi\":-71,\"channel\":11,\"secured\":false}]}",
		text.c_str());
}

static void test_empty_list_writes_an_empty_array()
{
	WifiScanList list;

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	list.toJson(root);

	std::string text;
	serializeJson(doc, text);

	TEST_ASSERT_EQUAL_STRING("{\"networks\":[]}", text.c_str());
}

static void test_bssid_text_is_upper_case_hex_pairs()
{
	TEST_ASSERT_EQUAL_STRING("12:34:56:78:9A:BC", w2b::BSSIDStr(w2b::BSSID{0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc}).c_str());
	TEST_ASSERT_EQUAL_STRING("AB:CD:EF:00:0F:F0", w2b::BSSIDStr(w2b::BSSID{0xab, 0xcd, 0xef, 0x00, 0x0f, 0xf0}).c_str());
	TEST_ASSERT_EQUAL_STRING("00:00:00:00:00:00", w2b::BSSIDStr(w2b::BSSID{}).c_str());
}

static void test_link_json_layout()
{
	w2b::WifiLink link;
	link.setHostname("w2b");
	link.setNetwork("home", w2b::BSSID{0xaa, 0xbb, 0xcc, 0x00, 0x11, 0x22}, 6);
	link.setAddresses(0xc0a80002, 0xffffff00, 0xc0a80001, 0xc0a80001);
	link.setRssi(-50);

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	link.toJson(root);

	std::string text;
	serializeJson(doc, text);

	TEST_ASSERT_TRUE(link.up());
	TEST_ASSERT_EQUAL_STRING("w2b", link.hostname().c_str());
	TEST_ASSERT_EQUAL_STRING("{\"ssid\":\"home\",\"connected\":true,\"address\":\"192.168.0.2\",\"mask\":\"255.255.255.0\","
		"\"gateway\":\"192.168.0.1\",\"dns\":\"192.168.0.1\",\"rssi\":-50,\"channel\":6,\"bssid\":\"AA:BB:CC:00:11:22\"}",
		text.c_str());
}

static void test_cleared_link_is_down_and_keeps_the_hostname()
{
	w2b::WifiLink link;
	link.setHostname("w2b");
	link.setNetwork("home", w2b::BSSID{0xaa, 0xbb, 0xcc, 0x00, 0x11, 0x22}, 6);
	link.setAddresses(0xc0a80002, 0xffffff00, 0xc0a80001, 0xc0a80001);
	link.setRssi(-50);
	link.clear();

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	link.toJson(root);

	std::string text;
	serializeJson(doc, text);

	TEST_ASSERT_FALSE(link.up());
	TEST_ASSERT_EQUAL_STRING("w2b", link.hostname().c_str());
	TEST_ASSERT_EQUAL_STRING("{\"ssid\":\"\",\"connected\":false,\"address\":\"0.0.0.0\",\"mask\":\"0.0.0.0\",\"gateway\":\"0.0.0.0\","
		"\"dns\":\"0.0.0.0\",\"rssi\":0,\"channel\":0,\"bssid\":\"\"}",
		text.c_str());
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_hidden_networks_are_left_out);
	RUN_TEST(test_strongest_first_equal_signals_keep_scan_order);
	RUN_TEST(test_one_entry_per_ssid_with_the_stronger_signal);
	RUN_TEST(test_a_full_list_drops_the_weakest);
	RUN_TEST(test_json_layout);
	RUN_TEST(test_empty_list_writes_an_empty_array);
	RUN_TEST(test_bssid_text_is_upper_case_hex_pairs);
	RUN_TEST(test_link_json_layout);
	RUN_TEST(test_cleared_link_is_down_and_keeps_the_hostname);

	return UNITY_END();
}
