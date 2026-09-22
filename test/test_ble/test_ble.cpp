#include <unity.h>

#include <string>

#include <ArduinoJson.h>

#include "BleAddress.h"
#include "BleScanList.h"
#include "BleUuid.h"
#include "GattTable.h"

using w2b::BleAddress;
using w2b::BleDevice;
using w2b::BleScanList;
using w2b::BleUuid;
using w2b::GattCharacteristic;
using w2b::GattService;
using w2b::GattTable;
using w2b::GattValue;

namespace
{

BleAddress addressOf(const char* text)
{
	BleAddress address;
	BleAddress::parse(text, address);

	return address;
}

BleUuid uuidOf(const char* text)
{
	BleUuid uuid;
	BleUuid::parse(text, uuid);

	return uuid;
}

BleDevice device(const char* address, const char* name, int8_t rssi)
{
	return BleDevice(addressOf(address), BleDevice::AddressType::Public, true, name, rssi);
}

std::string names(const BleScanList& list)
{
	std::string text;

	for (const BleDevice& found : list.devices())
	{
		text += std::string(found.name().c_str()) + " ";
	}

	return text;
}

template <typename T>
std::string jsonOf(const T& value)
{
	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	value.toJson(root);

	std::string text;
	serializeJson(doc, text);

	return text;
}

}  // namespace

void setUp()
{
}

void tearDown()
{
}

static void test_address_reads_six_hex_bytes_in_written_order()
{
	BleAddress address;

	TEST_ASSERT_TRUE(BleAddress::parse("12:34:56:78:9a:bc", address));
	TEST_ASSERT_EQUAL_HEX8(0x12, address.bytes()[0]);
	TEST_ASSERT_EQUAL_HEX8(0xbc, address.bytes()[5]);
	TEST_ASSERT_EQUAL_STRING("12:34:56:78:9a:bc", address.text().c_str());

	BleAddress upper;
	TEST_ASSERT_TRUE(BleAddress::parse("12:34:56:78:9A:BC", upper));
	TEST_ASSERT_TRUE(upper == address);
}

static void test_address_refuses_other_forms_and_keeps_its_value()
{
	BleAddress address;
	TEST_ASSERT_TRUE(BleAddress::parse("11:22:33:44:55:66", address));

	TEST_ASSERT_FALSE(BleAddress::parse("12:34:56:78:9a", address));
	TEST_ASSERT_FALSE(BleAddress::parse("d4-27-87-4a-9a-47", address));
	TEST_ASSERT_FALSE(BleAddress::parse("12:34:56:78:9a:bg", address));
	TEST_ASSERT_FALSE(BleAddress::parse("12:34:56:78:9a:bc ", address));
	TEST_ASSERT_FALSE(BleAddress::parse("", address));
	TEST_ASSERT_FALSE(BleAddress::parse(nullptr, address));

	TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", address.text().c_str());
}

static void test_uuid_keeps_the_form_it_was_written_in()
{
	BleUuid uuid;

	TEST_ASSERT_TRUE(BleUuid::parse("FEE7", uuid));
	TEST_ASSERT_EQUAL(2, uuid.size());
	TEST_ASSERT_EQUAL_HEX8(0xfe, uuid.bytes()[0]);
	TEST_ASSERT_EQUAL_STRING("fee7", uuid.text().c_str());

	TEST_ASSERT_TRUE(BleUuid::parse("0000fee7", uuid));
	TEST_ASSERT_EQUAL(4, uuid.size());
	TEST_ASSERT_EQUAL_STRING("0000fee7", uuid.text().c_str());

	TEST_ASSERT_TRUE(BleUuid::parse("0000FEE7-0000-1000-8000-00805F9B34FB", uuid));
	TEST_ASSERT_EQUAL(16, uuid.size());
	TEST_ASSERT_EQUAL_HEX8(0xfb, uuid.bytes()[15]);
	TEST_ASSERT_EQUAL_STRING("0000fee7-0000-1000-8000-00805f9b34fb", uuid.text().c_str());

	BleUuid shortForm;
	BleUuid::parse("fee7", shortForm);
	TEST_ASSERT_TRUE(shortForm != uuid);
}

static void test_uuid_empty_is_none_and_other_forms_are_refused()
{
	BleUuid uuid;
	TEST_ASSERT_TRUE(BleUuid::parse("fed5", uuid));

	TEST_ASSERT_FALSE(BleUuid::parse("fee", uuid));
	TEST_ASSERT_FALSE(BleUuid::parse("0xfee7", uuid));
	TEST_ASSERT_FALSE(BleUuid::parse("0000fee7-0000-1000-8000-00805f9b34f", uuid));
	TEST_ASSERT_FALSE(BleUuid::parse("0000fee700001000800000805f9b34fbxxxx", uuid));
	TEST_ASSERT_FALSE(BleUuid::parse("0000fee7-0000-1000-8000_00805f9b34fb", uuid));
	TEST_ASSERT_EQUAL_STRING("fed5", uuid.text().c_str());

	TEST_ASSERT_TRUE(BleUuid::parse("", uuid));
	TEST_ASSERT_TRUE(uuid.empty());
	TEST_ASSERT_EQUAL_STRING("", uuid.text().c_str());
}

static void test_uuid_from_bytes_in_written_order()
{
	const uint8_t shortBytes[] = {0xfe, 0xe7};
	TEST_ASSERT_TRUE(BleUuid(shortBytes, sizeof(shortBytes)) == uuidOf("fee7"));

	const uint8_t longBytes[] = {0x00, 0x00, 0xfe, 0xe7, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb};
	TEST_ASSERT_EQUAL_STRING("0000fee7-0000-1000-8000-00805f9b34fb", BleUuid(longBytes, sizeof(longBytes)).text().c_str());

	TEST_ASSERT_TRUE(BleUuid(longBytes, 3).empty());
	TEST_ASSERT_TRUE(BleUuid(nullptr, 2).empty());
}

static void test_uuid_compares_across_its_forms()
{
	TEST_ASSERT_EQUAL_STRING("0000fed5-0000-1000-8000-00805f9b34fb", uuidOf("fed5").to128Bit().text().c_str());
	TEST_ASSERT_EQUAL_STRING("12345678-0000-1000-8000-00805f9b34fb", uuidOf("12345678").to128Bit().text().c_str());
	TEST_ASSERT_EQUAL_STRING("6e400001-b5a3-f393-e0a9-e50e24dcca9e", uuidOf("6E400001-B5A3-F393-E0A9-E50E24DCCA9E").to128Bit().text().c_str());
	TEST_ASSERT_TRUE(BleUuid().to128Bit().empty());

	TEST_ASSERT_TRUE(uuidOf("fed5").equals(uuidOf("0000fed5")));
	TEST_ASSERT_TRUE(uuidOf("fed5").equals(uuidOf("0000FED5-0000-1000-8000-00805F9B34FB")));
	TEST_ASSERT_FALSE(uuidOf("fed5").equals(uuidOf("fed6")));
	TEST_ASSERT_FALSE(uuidOf("fed5").equals(uuidOf("0000fed5-0000-1000-8000-00805f9b34fc")));
	TEST_ASSERT_FALSE(uuidOf("fed5").equals(BleUuid()));
}

static void test_scan_list_one_entry_per_address_strongest_first()
{
	BleScanList list;
	list.add(device("11:11:11:11:11:11", "far", -90));
	list.add(device("22:22:22:22:22:22", "near", -50));
	list.add(device("33:33:33:33:33:33", "mid", -70));
	list.add(device("44:44:44:44:44:44", "mid2", -70));
	TEST_ASSERT_EQUAL_STRING("near mid mid2 far ", names(list).c_str());

	// a stronger report moves the entry up, a weaker one keeps the strongest signal and the place
	list.add(device("11:11:11:11:11:11", "", -40));
	list.add(device("33:33:33:33:33:33", "", -95));
	TEST_ASSERT_EQUAL_STRING("far near mid mid2 ", names(list).c_str());
	TEST_ASSERT_EQUAL(-40, list.devices()[0].rssi());
	TEST_ASSERT_EQUAL(-70, list.devices()[2].rssi());
	TEST_ASSERT_EQUAL(4, list.devices().size());
}

static void test_scan_list_merges_name_services_and_manufacturer_data()
{
	const uint8_t first[] = {0x4c, 0x00, 0x01};
	const uint8_t second[] = {0x59, 0x00};

	BleDevice advertisement = device("12:34:56:78:9a:bc", "", -60);
	advertisement.addService(uuidOf("fee7"));
	advertisement.setManufacturerData(first, sizeof(first));

	BleDevice scanResponse = device("12:34:56:78:9a:bc", "HF-LPT270", -62);
	scanResponse.addService(uuidOf("fee7"));
	scanResponse.addService(uuidOf("180f"));
	scanResponse.setManufacturerData(second, sizeof(second));

	BleScanList list;
	list.add(advertisement);
	list.add(scanResponse);
	list.add(device("12:34:56:78:9a:bc", "", -61));

	TEST_ASSERT_EQUAL(1, list.devices().size());
	const BleDevice& merged = list.devices()[0];
	TEST_ASSERT_EQUAL_STRING("HF-LPT270", merged.name().c_str());
	TEST_ASSERT_EQUAL(-60, merged.rssi());
	TEST_ASSERT_EQUAL(2, merged.serviceCount());
	TEST_ASSERT_EQUAL_STRING("180f", merged.services()[1].text().c_str());
	TEST_ASSERT_EQUAL(2, merged.manufacturerSize());
	TEST_ASSERT_EQUAL_HEX8(0x59, merged.manufacturerData()[0]);
}

static void test_device_limits_services_and_manufacturer_data()
{
	BleDevice found = device("11:22:33:44:55:66", "", -70);
	found.addService(BleUuid());

	for (size_t i = 0; i < BleDevice::kMaxServices + 2; ++i)
	{
		const uint8_t bytes[] = {0x18, static_cast<uint8_t>(i)};
		found.addService(BleUuid(bytes, sizeof(bytes)));
	}

	TEST_ASSERT_EQUAL(BleDevice::kMaxServices, found.serviceCount());

	uint8_t data[BleDevice::kMaxFieldBytes + 5] = {};
	found.setManufacturerData(data, sizeof(data));
	TEST_ASSERT_EQUAL(BleDevice::kMaxFieldBytes, found.manufacturerSize());

	found.setManufacturerData(nullptr, 4);
	TEST_ASSERT_EQUAL(0, found.manufacturerSize());
}

static void test_scan_list_keeps_every_device()
{
	BleScanList list;

	for (size_t i = 0; i < 200; ++i)
	{
		const BleAddress::Bytes bytes = {0x02, 0, 0, 0, static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
		list.add(BleDevice(BleAddress(bytes), BleDevice::AddressType::Random, true, ("d" + std::to_string(i)).c_str(),
			static_cast<int8_t>(-30 - static_cast<int>(i % 70))));
	}

	TEST_ASSERT_EQUAL(200, list.devices().size());
	TEST_ASSERT_EQUAL_STRING("d0", list.devices().front().name().c_str());
	TEST_ASSERT_EQUAL(-99, list.devices().back().rssi());

	list.clear();
	TEST_ASSERT_EQUAL(0, list.devices().size());
}

static void test_scan_list_json_layout()
{
	const uint8_t maker[] = {0x4c, 0x00, 0x0a};

	BleDevice module = device("12:34:56:78:9a:bc", "HF-LPT270", -62);
	module.addService(uuidOf("fee7"));
	module.setManufacturerData(maker, sizeof(maker));

	BleScanList list;
	list.add(BleDevice(addressOf("e3:7a:1c:44:90:2f"), BleDevice::AddressType::Random, false, "", -88));
	list.add(module);

	TEST_ASSERT_EQUAL_STRING("{\"devices\":[{\"address\":\"12:34:56:78:9a:bc\",\"address_type\":\"public\",\"rssi\":-62,"
		"\"connectable\":true,\"name\":\"HF-LPT270\",\"services\":[\"fee7\"],\"manufacturer_data\":\"4c000a\"},{\"address\":\"e3:7a:1c:44:90:2f\","
		"\"address_type\":\"random\",\"rssi\":-88,\"connectable\":false,\"name\":\"\",\"services\":[],\"manufacturer_data\":\"\"}]}",
		jsonOf(list).c_str());

	TEST_ASSERT_EQUAL_STRING("{\"devices\":[]}", jsonOf(BleScanList()).c_str());
}

static void test_scan_list_keeps_the_address_type_and_merges_connectable()
{
	BleScanList list;
	list.add(BleDevice(addressOf("11:11:11:11:11:11"), BleDevice::AddressType::Public, false, "public", -60));
	list.add(BleDevice(addressOf("22:22:22:22:22:22"), BleDevice::AddressType::Random, true, "random", -70));

	// a later report keeps the type the entry was found with; connectable when either report is
	list.add(BleDevice(addressOf("11:11:11:11:11:11"), BleDevice::AddressType::Random, true, "", -50));
	list.add(BleDevice(addressOf("22:22:22:22:22:22"), BleDevice::AddressType::Random, false, "", -75));

	TEST_ASSERT_EQUAL(2, list.devices().size());
	const BleDevice& first = list.devices()[0];
	TEST_ASSERT_TRUE(first.address() == addressOf("11:11:11:11:11:11"));
	TEST_ASSERT_TRUE(first.addressType() == BleDevice::AddressType::Public);
	TEST_ASSERT_TRUE(first.isConnectable());
	TEST_ASSERT_EQUAL(-50, first.rssi());

	const BleDevice& second = list.devices()[1];
	TEST_ASSERT_TRUE(second.addressType() == BleDevice::AddressType::Random);
	TEST_ASSERT_TRUE(second.isConnectable());
	TEST_ASSERT_EQUAL_STRING("random", second.name().c_str());
}

static void test_gatt_table_counts_and_keeps_handle_order()
{
	GattService access(uuidOf("1800"));
	access.add(GattCharacteristic(uuidOf("2a00"), 0x02));

	GattService module(uuidOf("fee7"));
	module.add(GattCharacteristic(uuidOf("fed5"), 0x08));
	module.add(GattCharacteristic(uuidOf("fed6"), 0x20));
	module.add(GattCharacteristic(uuidOf("fed4"), 0x14));

	GattTable table;
	TEST_ASSERT_EQUAL(0, table.characteristicCount());

	table.add(access);
	table.add(module);
	table.add(GattService(uuidOf("180a")));

	TEST_ASSERT_EQUAL(3, table.services().size());
	TEST_ASSERT_EQUAL(4, table.characteristicCount());
	TEST_ASSERT_EQUAL_STRING("fee7", table.services()[1].uuid().text().c_str());
	TEST_ASSERT_EQUAL_STRING("fed4", table.services()[1].characteristics()[2].uuid().text().c_str());
	TEST_ASSERT_EQUAL_HEX8(0x14, table.services()[1].characteristics()[2].properties());
}

static void test_gatt_table_json_layout()
{
	GattService module(uuidOf("0000fee7-0000-1000-8000-00805f9b34fb"));
	module.add(GattCharacteristic(uuidOf("fed5"), 0x08));
	module.add(GattCharacteristic(uuidOf("fed6"), 0x20));

	const uint8_t maker[] = {'A', 'c', 'm', 'e'};
	const uint8_t appearance[] = {0x40, 0x00};

	GattTable table;
	table.add(module);
	table.add(GattService(uuidOf("180a")));
	table.add(GattValue(uuidOf("2a29"), maker, sizeof(maker)));
	table.add(GattValue(uuidOf("2a01"), appearance, sizeof(appearance)));

	TEST_ASSERT_EQUAL_STRING("{\"services\":[{\"uuid\":\"0000fee7-0000-1000-8000-00805f9b34fb\",\"characteristics\":[{\"uuid\":\"fed5\","
		"\"properties\":8},{\"uuid\":\"fed6\",\"properties\":32}]},{\"uuid\":\"180a\",\"characteristics\":[]}],"
		"\"values\":[{\"uuid\":\"2a29\",\"value\":\"41636d65\"},{\"uuid\":\"2a01\",\"value\":\"4000\"}]}",
		jsonOf(table).c_str());

	TEST_ASSERT_EQUAL_STRING("{\"services\":[],\"values\":[]}", jsonOf(GattTable()).c_str());
}

static void test_gatt_value_keeps_its_first_bytes()
{
	uint8_t name[GattValue::kMaxBytes + 10] = {};

	for (size_t i = 0; i < sizeof(name); ++i)
	{
		name[i] = static_cast<uint8_t>(i);
	}

	const GattValue longer(uuidOf("2a00"), name, sizeof(name));
	TEST_ASSERT_EQUAL(GattValue::kMaxBytes, longer.size());
	TEST_ASSERT_EQUAL_HEX8(GattValue::kMaxBytes - 1, longer.bytes()[GattValue::kMaxBytes - 1]);
	TEST_ASSERT_EQUAL_STRING("2a00", longer.uuid().text().c_str());

	TEST_ASSERT_EQUAL(3, GattValue(uuidOf("2a24"), name, 3).size());
	TEST_ASSERT_EQUAL(0, GattValue(uuidOf("2a24"), nullptr, 3).size());
	TEST_ASSERT_EQUAL_STRING("{\"uuid\":\"2a24\",\"value\":\"\"}", jsonOf(GattValue(uuidOf("2a24"), name, 0)).c_str());
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_address_reads_six_hex_bytes_in_written_order);
	RUN_TEST(test_address_refuses_other_forms_and_keeps_its_value);
	RUN_TEST(test_uuid_keeps_the_form_it_was_written_in);
	RUN_TEST(test_uuid_empty_is_none_and_other_forms_are_refused);
	RUN_TEST(test_uuid_from_bytes_in_written_order);
	RUN_TEST(test_uuid_compares_across_its_forms);
	RUN_TEST(test_scan_list_one_entry_per_address_strongest_first);
	RUN_TEST(test_scan_list_merges_name_services_and_manufacturer_data);
	RUN_TEST(test_device_limits_services_and_manufacturer_data);
	RUN_TEST(test_scan_list_keeps_every_device);
	RUN_TEST(test_scan_list_json_layout);
	RUN_TEST(test_scan_list_keeps_the_address_type_and_merges_connectable);
	RUN_TEST(test_gatt_table_counts_and_keeps_handle_order);
	RUN_TEST(test_gatt_table_json_layout);
	RUN_TEST(test_gatt_value_keeps_its_first_bytes);

	return UNITY_END();
}
