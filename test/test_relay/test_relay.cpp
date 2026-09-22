#include <unity.h>

#include <algorithm>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "ChannelHandles.h"
#include "DeviceRules.h"
#include "JsonText.h"
#include "PacketRing.h"
#include "RelayStatus.h"
#include "TcpClientData.h"
#include "WriteMode.h"

using w2b::BleAddress;
using w2b::BleUuid;
using w2b::ChannelHandles;
using w2b::ConfigData;
using w2b::DeviceRules;
using w2b::DiscoveredCharacteristic;
using w2b::JsonText;
using w2b::LinkState;
using w2b::PacketRing;
using w2b::RelayCounters;
using w2b::RelayStatus;
using w2b::RuleConfig;
using w2b::SerializableFactory;
using w2b::TcpClientData;
using w2b::TcpStatus;
using w2b::WhenFull;
using w2b::WriteMode;

namespace
{

BleUuid uuid(const char* text)
{
	BleUuid parsed;
	BleUuid::parse(text, parsed);

	return parsed;
}

BleAddress address(const char* text)
{
	BleAddress parsed;
	BleAddress::parse(text, parsed);

	return parsed;
}

// A valid rule: extra's keys over TX FED5 and RX FED6 in service FEE7 of the device at target.
RuleConfig rule(const std::string& id, const std::string& target, const std::string& extra = "")
{
	RuleConfig made = SerializableFactory::createEmpty<RuleConfig>();
	std::string error;
	const std::string json = R"({"id":")" + id + R"(","target":)" + target + R"(,"service_uuid":"fee7",)"
		R"("tx":{"mode":"uuid","uuid":"fed5"},"rx":{"mode":"uuid","uuid":"fed6"})" + (extra.empty() ? "" : "," + extra) + "}";

	TEST_ASSERT_TRUE_MESSAGE(JsonText::read(json, made, error), error.c_str());

	return made;
}

const std::string kModule = R"({"address":"12:34:56:78:9a:bc","name":"HF-LPT270"})";

// The HF-LPT270's FEE7: FED5 write, FED6 indicate, FEC7 write without response, FEC8 read and notify, FED4 write without response and
// notify.
std::vector<DiscoveredCharacteristic> moduleService()
{
	return {DiscoveredCharacteristic(uuid("fed5"), 0x08, 0x0012, 0x0012), DiscoveredCharacteristic(uuid("fed6"), 0x20, 0x0014, 0x0015),
		DiscoveredCharacteristic(uuid("fec7"), 0x04, 0x0017, 0x0017), DiscoveredCharacteristic(uuid("fec8"), 0x12, 0x0019, 0x001a),
		DiscoveredCharacteristic(uuid("fed4"), 0x14, 0x001c, 0x001d)};
}

std::string jsonOf(const RelayStatus& status)
{
	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	status.toJson(root);

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

static void test_enabled_rules_are_grouped_by_device()
{
	const ConfigData::Rules rules = {
		rule("a", kModule),
		rule("off", R"({"address":"11:11:11:11:11:11"})", R"("enabled":false)"),
		rule("b", R"({"address":"12:34:56:78:9A:BC"})", R"("listen_port":9000)"),
		rule("c", R"({"name":"HF-LPT270"})", R"("listen_port":9001)"),
		rule("d", R"({"name":"HF-LPT270"})", R"("listen_port":9002)"),
	};

	const std::vector<DeviceRules> devices = DeviceRules::groupByDevice(rules);

	// by address the same device in either case; by name another, since a name-only rule's device is known only once found
	TEST_ASSERT_EQUAL(2, devices.size());
	TEST_ASSERT_EQUAL(2, devices[0].rules().size());
	TEST_ASSERT_EQUAL_STRING("b", devices[0].rules()[1].id().c_str());
	TEST_ASSERT_TRUE(devices[0].target().address() == address("12:34:56:78:9a:bc"));
	TEST_ASSERT_EQUAL(2, devices[1].rules().size());
	TEST_ASSERT_FALSE(devices[1].target().address().has_value());
	TEST_ASSERT_EQUAL_STRING("HF-LPT270", devices[1].target().name().c_str());

	TEST_ASSERT_EQUAL(0, DeviceRules::groupByDevice({rule("off", kModule, R"("enabled":false)")}).size());
}

static void test_a_device_wants_its_link_for_a_rule_that_is_always_up()
{
	TEST_ASSERT_FALSE(DeviceRules::groupByDevice({rule("a", kModule)})[0].wantsLink());
	TEST_ASSERT_TRUE(DeviceRules::groupByDevice({rule("a", kModule), rule("b", kModule, R"("listen_port":9000,"connect_policy":"always")")})[0].wantsLink());
}

static void test_the_backoff_follows_the_first_rule_its_last_wait_repeating()
{
	const DeviceRules device = DeviceRules::groupByDevice({rule("a", kModule, R"("reconnect_backoff_s":[1,5])"), rule("b", kModule, R"("listen_port":9000)")})[0];

	TEST_ASSERT_EQUAL_UINT32(1000, device.backoffMs(0));
	TEST_ASSERT_EQUAL_UINT32(5000, device.backoffMs(1));
	TEST_ASSERT_EQUAL_UINT32(5000, device.backoffMs(7));
	TEST_ASSERT_EQUAL(2, device.backoffSteps());
	TEST_ASSERT_EQUAL(5, DeviceRules::groupByDevice({rule("a", kModule)})[0].backoffSteps());
}

static void test_devices_compare_by_target_and_rules()
{
	const DeviceRules first = DeviceRules::groupByDevice({rule("a", kModule)})[0];

	TEST_ASSERT_TRUE(first == DeviceRules::groupByDevice({rule("a", kModule)})[0]);
	TEST_ASSERT_TRUE(first != DeviceRules::groupByDevice({rule("a", kModule, R"("hexdump":true)")})[0]);
	TEST_ASSERT_TRUE(first != DeviceRules::groupByDevice({rule("a", R"({"address":"11:11:11:11:11:11"})")})[0]);
}

static void test_find_takes_tx_and_rx_by_uuid_in_any_form()
{
	ChannelHandles handles;
	std::string error;

	TEST_ASSERT_TRUE_MESSAGE(ChannelHandles::find(rule("a", kModule), moduleService(), handles, error), error.c_str());
	TEST_ASSERT_EQUAL_HEX16(0x0012, handles.txHandle());
	TEST_ASSERT_TRUE(handles.txWithResponse());
	TEST_ASSERT_EQUAL_HEX16(0x0014, handles.rxHandle());
	TEST_ASSERT_EQUAL_HEX16(0x0015, handles.rxEnd());
	TEST_ASSERT_TRUE(handles.rxIndicate());

	const RuleConfig longForms = rule("a", kModule,
		R"("tx":{"mode":"uuid","uuid":"0000fed5"},"rx":{"mode":"uuid","uuid":"0000FED6-0000-1000-8000-00805F9B34FB"})");
	ChannelHandles same;
	TEST_ASSERT_TRUE_MESSAGE(ChannelHandles::find(longForms, moduleService(), same, error), error.c_str());
	TEST_ASSERT_TRUE(same == handles);
}

static void test_find_writes_as_the_characteristic_allows()
{
	ChannelHandles handles;
	std::string error;

	// with response asked, only without offered: without
	TEST_ASSERT_TRUE(ChannelHandles::find(rule("a", kModule, R"("tx":{"mode":"uuid","uuid":"fec7"})"), moduleService(), handles, error));
	TEST_ASSERT_FALSE(handles.txWithResponse());

	// without asked, only with offered: with
	TEST_ASSERT_TRUE(ChannelHandles::find(rule("a", kModule, R"("tx":{"mode":"uuid","uuid":"fed5","write_with_response":false})"), moduleService(), handles,
		error));
	TEST_ASSERT_TRUE(handles.txWithResponse());

	TEST_ASSERT_FALSE(ChannelHandles::find(rule("a", kModule, R"("tx":{"mode":"uuid","uuid":"fed6"})"), moduleService(), handles, error));
	TEST_ASSERT_EQUAL_STRING("tx fed6: neither write nor write without response", error.c_str());
}

static void test_find_subscribes_as_asked_and_offered()
{
	ChannelHandles handles;
	std::string error;

	TEST_ASSERT_TRUE(ChannelHandles::find(rule("a", kModule, R"("rx":{"mode":"uuid","uuid":"fec8"})"), moduleService(), handles, error));
	TEST_ASSERT_FALSE(handles.rxIndicate());

	TEST_ASSERT_FALSE(ChannelHandles::find(rule("a", kModule, R"("rx":{"mode":"uuid","uuid":"fec8","subscribe":"indicate"})"), moduleService(), handles,
		error));
	TEST_ASSERT_EQUAL_STRING("rx fec8: no indicate", error.c_str());

	TEST_ASSERT_FALSE(ChannelHandles::find(rule("a", kModule, R"("rx":{"mode":"uuid","uuid":"fed6","subscribe":"notify"})"), moduleService(), handles,
		error));
	TEST_ASSERT_EQUAL_STRING("rx fed6: no notify", error.c_str());

	TEST_ASSERT_FALSE(ChannelHandles::find(rule("a", kModule, R"("rx":{"mode":"uuid","uuid":"fed5"})"), moduleService(), handles, error));
	TEST_ASSERT_EQUAL_STRING("rx fed5: neither notify nor indicate", error.c_str());
}

static void test_find_names_a_missing_characteristic_and_skips_a_channel_of_none()
{
	ChannelHandles handles;
	std::string error;

	TEST_ASSERT_FALSE(ChannelHandles::find(rule("a", kModule, R"("tx":{"mode":"uuid","uuid":"abcd"})"), moduleService(), handles, error));
	TEST_ASSERT_EQUAL_STRING("tx abcd: not in service fee7", error.c_str());

	TEST_ASSERT_TRUE_MESSAGE(ChannelHandles::find(rule("a", kModule, R"("tx":{"mode":"none"})"), moduleService(), handles, error), error.c_str());
	TEST_ASSERT_EQUAL_HEX16(0, handles.txHandle());
	TEST_ASSERT_EQUAL_HEX16(0x0014, handles.rxHandle());
}

using DropRing = PacketRing<WhenFull::DropOldest, 16>;

using RefuseRing = PacketRing<WhenFull::Refuse, 16>;

// size bytes counting up from first
std::vector<uint8_t> bytesFrom(uint8_t first, size_t size)
{
	std::vector<uint8_t> bytes(size);

	for (size_t i = 0; i < size; ++i)
	{
		bytes[i] = static_cast<uint8_t>(first + i);
	}

	return bytes;
}

static void test_the_ring_keeps_packets_in_order()
{
	DropRing ring;
	DropRing::Reader reader = ring.createReader();
	uint8_t out[8] = {};

	TEST_ASSERT_TRUE(ring.isEmpty());
	TEST_ASSERT_EQUAL(0, ring.read(reader, out, sizeof(out)));

	TEST_ASSERT_TRUE(ring.push(bytesFrom(1, 3).data(), 3));
	TEST_ASSERT_TRUE(ring.push(bytesFrom(10, 2).data(), 2));
	TEST_ASSERT_EQUAL(9, ring.size());

	TEST_ASSERT_EQUAL(3, ring.read(reader, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(1, 3).data(), out, 3);
	TEST_ASSERT_EQUAL(2, ring.read(reader, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(10, 2).data(), out, 2);
	TEST_ASSERT_EQUAL(0, ring.read(reader, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT32(0, reader.lost());
}

// Every size from 1 to 9 in turn moves where packets start, so lengths and bytes cross the ring's end at every point.
static void test_the_ring_splits_packets_across_its_end()
{
	RefuseRing ring;
	DropRing drop;
	DropRing::Reader reader = drop.createReader();
	uint8_t out[16] = {};

	for (size_t round = 0; round < 60; ++round)
	{
		const size_t size = 1 + round % 9;
		const std::vector<uint8_t> packet = bytesFrom(static_cast<uint8_t>(round * 7), size);

		TEST_ASSERT_TRUE(ring.push(packet.data(), size));
		TEST_ASSERT_TRUE(drop.push(packet.data(), size));

		TEST_ASSERT_EQUAL(size, ring.peek(out, sizeof(out), 0));
		TEST_ASSERT_EQUAL_UINT8_ARRAY(packet.data(), out, size);
		ring.pop();
		TEST_ASSERT_TRUE(ring.isEmpty());

		TEST_ASSERT_EQUAL(size, drop.read(reader, out, sizeof(out)));
		TEST_ASSERT_EQUAL_UINT8_ARRAY(packet.data(), out, size);
	}

	TEST_ASSERT_EQUAL_UINT32(0, reader.lost());
	TEST_ASSERT_EQUAL_UINT32(0, ring.refused());
}

static void test_the_ring_drops_the_oldest_whole_packets_for_a_new_one()
{
	DropRing ring;
	DropRing::Reader reader = ring.createReader();
	uint8_t out[16] = {};

	// 7 bytes each with their lengths: two fit in 16, the third drops the first
	TEST_ASSERT_TRUE(ring.push(bytesFrom(1, 5).data(), 5));
	TEST_ASSERT_TRUE(ring.push(bytesFrom(11, 5).data(), 5));
	TEST_ASSERT_TRUE(ring.push(bytesFrom(21, 5).data(), 5));
	TEST_ASSERT_EQUAL_UINT32(1, ring.dropped());
	TEST_ASSERT_EQUAL(14, ring.size());

	TEST_ASSERT_EQUAL(5, ring.read(reader, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(11, 5).data(), out, 5);
	TEST_ASSERT_EQUAL_UINT32(1, reader.lost());

	// one long packet drops both that are left
	TEST_ASSERT_TRUE(ring.push(bytesFrom(31, 12).data(), 12));
	TEST_ASSERT_EQUAL_UINT32(3, ring.dropped());
	TEST_ASSERT_EQUAL(12, ring.read(reader, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(31, 12).data(), out, 12);
	TEST_ASSERT_EQUAL_UINT32(2, reader.lost());
}

static void test_readers_keep_their_own_place()
{
	DropRing ring;
	DropRing::Reader early = ring.createReader();
	uint8_t out[16] = {};

	TEST_ASSERT_TRUE(ring.push(bytesFrom(1, 2).data(), 2));
	DropRing::Reader late = ring.createReader();
	TEST_ASSERT_TRUE(ring.push(bytesFrom(5, 3).data(), 3));

	// early reads both; late only what came after it
	TEST_ASSERT_EQUAL(3, ring.read(late, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(5, 3).data(), out, 3);
	TEST_ASSERT_EQUAL(0, ring.read(late, out, sizeof(out)));
	TEST_ASSERT_EQUAL(2, ring.read(early, out, sizeof(out)));
	TEST_ASSERT_EQUAL(3, ring.read(early, out, sizeof(out)));
	TEST_ASSERT_EQUAL(0, ring.read(early, out, sizeof(out)));
}

static void test_a_reader_takes_a_packet_in_parts()
{
	DropRing ring;
	DropRing::Reader reader = ring.createReader();
	uint8_t out[16] = {};

	TEST_ASSERT_TRUE(ring.push(bytesFrom(1, 6).data(), 6));
	TEST_ASSERT_TRUE(ring.push(bytesFrom(20, 3).data(), 3));

	// peek() leaves the reader where it is
	TEST_ASSERT_EQUAL(6, ring.peek(reader, out, 4));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(1, 4).data(), out, 4);
	TEST_ASSERT_EQUAL(6, ring.peek(reader, out, sizeof(out)));

	ring.skip(reader, 4);
	TEST_ASSERT_EQUAL(2, ring.peek(reader, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(5, 2).data(), out, 2);

	// read() takes what is left; skips of 1 and 2 bytes take the next packet; a skip past the last does nothing
	TEST_ASSERT_EQUAL(2, ring.read(reader, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(5, 2).data(), out, 2);
	ring.skip(reader, 1);
	TEST_ASSERT_EQUAL(2, ring.peek(reader, out, sizeof(out)));
	ring.skip(reader, 2);
	TEST_ASSERT_EQUAL(0, ring.peek(reader, out, sizeof(out)));
	ring.skip(reader, 1);
	TEST_ASSERT_EQUAL(0, ring.read(reader, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT32(0, reader.lost());
}

static void test_a_reader_whose_packet_is_dropped_meanwhile_starts_at_the_oldest()
{
	DropRing ring;
	DropRing::Reader reader = ring.createReader();
	uint8_t out[16] = {};

	TEST_ASSERT_TRUE(ring.push(bytesFrom(1, 5).data(), 5));
	TEST_ASSERT_EQUAL(5, ring.peek(reader, out, sizeof(out)));
	ring.skip(reader, 2);

	// two more packets of 7 bytes with their lengths drop the one half read
	TEST_ASSERT_TRUE(ring.push(bytesFrom(11, 5).data(), 5));
	TEST_ASSERT_TRUE(ring.push(bytesFrom(21, 5).data(), 5));
	TEST_ASSERT_EQUAL_UINT32(1, ring.dropped());

	// the bytes sent from the dropped packet move nothing in the next one
	ring.skip(reader, 3);
	TEST_ASSERT_EQUAL_UINT32(1, reader.lost());
	TEST_ASSERT_EQUAL(5, ring.peek(reader, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(11, 5).data(), out, 5);

	// a peek finds the loss as well, and starts the oldest packet from its first byte
	ring.skip(reader, 4);
	TEST_ASSERT_TRUE(ring.push(bytesFrom(31, 5).data(), 5));
	TEST_ASSERT_EQUAL(5, ring.peek(reader, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(21, 5).data(), out, 5);
	TEST_ASSERT_EQUAL_UINT32(2, reader.lost());
}

static void test_the_ring_refuses_empty_and_too_long_packets_and_cuts_to_room()
{
	DropRing ring;
	DropRing::Reader reader = ring.createReader();
	uint8_t out[3] = {};

	TEST_ASSERT_FALSE(ring.push(nullptr, 1));
	TEST_ASSERT_FALSE(ring.push(bytesFrom(0, 1).data(), 0));
	TEST_ASSERT_FALSE(ring.push(bytesFrom(0, DropRing::kMaxPacket + 1).data(), DropRing::kMaxPacket + 1));
	TEST_ASSERT_EQUAL_UINT32(3, ring.refused());

	// the longest packet fills the ring; a read with less room gets its first bytes and its full size, and moves on
	TEST_ASSERT_TRUE(ring.push(bytesFrom(40, DropRing::kMaxPacket).data(), DropRing::kMaxPacket));
	TEST_ASSERT_EQUAL(16, ring.size());
	TEST_ASSERT_EQUAL(DropRing::kMaxPacket, ring.read(reader, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(40, 3).data(), out, 3);
	TEST_ASSERT_EQUAL(0, ring.read(reader, out, sizeof(out)));
}

static void test_a_refusing_ring_keeps_its_packets_until_popped()
{
	RefuseRing ring;
	uint8_t out[16] = {};

	TEST_ASSERT_TRUE(ring.push(bytesFrom(1, 5).data(), 5));
	TEST_ASSERT_TRUE(ring.push(bytesFrom(11, 5).data(), 5));
	TEST_ASSERT_FALSE(ring.push(bytesFrom(21, 5).data(), 5));
	TEST_ASSERT_EQUAL_UINT32(1, ring.refused());
	TEST_ASSERT_EQUAL_UINT32(0, ring.dropped());

	ring.pop();
	TEST_ASSERT_TRUE(ring.push(bytesFrom(21, 5).data(), 5));
	TEST_ASSERT_EQUAL(5, ring.peek(out, sizeof(out), 0));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(11, 5).data(), out, 5);
	ring.pop();
	TEST_ASSERT_EQUAL(5, ring.peek(out, sizeof(out), 0));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(21, 5).data(), out, 5);
	ring.pop();
	TEST_ASSERT_TRUE(ring.isEmpty());
	TEST_ASSERT_EQUAL(0, ring.peek(out, sizeof(out), 0));
}

static void test_an_open_packet_shows_once_closed()
{
	RefuseRing ring;
	uint8_t out[16] = {};

	TEST_ASSERT_EQUAL(14, ring.spaceLeft());
	TEST_ASSERT_TRUE(ring.append(bytesFrom(1, 3).data(), 3));
	TEST_ASSERT_TRUE(ring.append(bytesFrom(4, 2).data(), 2));
	TEST_ASSERT_EQUAL(5, ring.openSize());
	TEST_ASSERT_EQUAL(9, ring.spaceLeft());
	TEST_ASSERT_TRUE(ring.isEmpty());
	TEST_ASSERT_EQUAL(0, ring.peek(out, sizeof(out), 0));

	// nothing is pushed past an open packet
	TEST_ASSERT_FALSE(ring.push(bytesFrom(9, 1).data(), 1));

	ring.close();
	TEST_ASSERT_EQUAL(5, ring.peek(out, sizeof(out), 0));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(1, 5).data(), out, 5);
	TEST_ASSERT_EQUAL(0, ring.openSize());

	// 7 of 16 used: 7 more with a new length fit, 8 do not
	TEST_ASSERT_FALSE(ring.append(bytesFrom(0, 8).data(), 8));
	TEST_ASSERT_TRUE(ring.append(bytesFrom(0, 7).data(), 7));
	TEST_ASSERT_EQUAL(0, ring.spaceLeft());
	ring.close();
	ring.pop();
	ring.pop();
	TEST_ASSERT_EQUAL(0, ring.size());

	// appending nothing opens no packet
	TEST_ASSERT_TRUE(ring.append(nullptr, 0));
	ring.close();
	TEST_ASSERT_EQUAL(0, ring.size());
	TEST_ASSERT_TRUE(ring.isEmpty());
}

// A packet read in chunks from an offset, as the BLE writes take it, while it wraps around the ring's end.
static void test_an_open_packet_wraps_and_is_read_in_chunks()
{
	RefuseRing ring;
	uint8_t out[16] = {};

	TEST_ASSERT_TRUE(ring.push(bytesFrom(0, 8).data(), 8));
	ring.pop();

	// starts at 10 of 16: its length and first bytes before the end, the rest after
	TEST_ASSERT_TRUE(ring.append(bytesFrom(50, 4).data(), 4));
	TEST_ASSERT_TRUE(ring.append(bytesFrom(54, 6).data(), 6));
	ring.close();

	for (size_t from = 0; from < 10; from += 3)
	{
		const size_t count = std::min<size_t>(3, 10 - from);
		TEST_ASSERT_EQUAL(10, ring.peek(out, 3, from));
		TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(static_cast<uint8_t>(50 + from), count).data(), out, count);
	}
}

static void test_client_data_closes_a_packet_after_the_silence()
{
	TcpClientData data;
	uint8_t out[8] = {};

	TEST_ASSERT_TRUE(data.add(bytesFrom(1, 2).data(), 2));
	data.update(10, 20, 0);
	TEST_ASSERT_TRUE(data.add(bytesFrom(3, 2).data(), 2));

	// the update after bytes counts no silence; then 10 + 10 ms reach 20
	data.update(10, 20, 0);
	TEST_ASSERT_FALSE(data.hasPacket());
	data.update(10, 20, 0);
	TEST_ASSERT_FALSE(data.hasPacket());
	data.update(10, 20, 0);
	TEST_ASSERT_TRUE(data.hasPacket());
	TEST_ASSERT_EQUAL(4, data.peek(out, sizeof(out), 0));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesFrom(1, 4).data(), out, 4);

	data.update(10, 20, 0);
	TEST_ASSERT_EQUAL_UINT32(10, data.waitedMs());
	data.pop();
	TEST_ASSERT_FALSE(data.hasPacket());
	TEST_ASSERT_EQUAL_UINT32(0, data.waitedMs());
}

static void test_client_data_closes_at_once_at_a_chunk_or_when_full()
{
	TcpClientData data;
	uint8_t out[8] = {};

	// coalesce_ms 0: the bytes of one update make one packet
	TEST_ASSERT_TRUE(data.add(bytesFrom(1, 3).data(), 3));
	data.update(10, 0, 0);
	TEST_ASSERT_EQUAL(3, data.peek(out, sizeof(out), 0));
	data.pop();

	// a chunk's size closes it before the silence
	TEST_ASSERT_TRUE(data.add(bytesFrom(1, 6).data(), 6));
	data.update(10, 100, 5);
	TEST_ASSERT_EQUAL(6, data.peek(out, sizeof(out), 0));
	data.pop();

	// full: no more space, closed without silence
	const std::vector<uint8_t> big = bytesFrom(0, TcpClientData::kMaxPacket);
	TEST_ASSERT_TRUE(data.add(big.data(), big.size()));
	TEST_ASSERT_EQUAL(0, data.spaceLeft());
	TEST_ASSERT_FALSE(data.add(big.data(), 1));
	data.update(10, 100, 0);
	TEST_ASSERT_EQUAL(TcpClientData::kMaxPacket, data.peek(nullptr, 0, 0));
}

static void test_write_mode_follows_the_mtu_and_the_oversize_policy()
{
	TEST_ASSERT_EQUAL(20, w2b::maxWriteBytes(23));
	TEST_ASSERT_EQUAL(252, w2b::maxWriteBytes(255));
	TEST_ASSERT_EQUAL(0, w2b::maxWriteBytes(0));

	TEST_ASSERT_TRUE(w2b::chooseWriteMode(20, 23, RuleConfig::Oversize::Drop) == WriteMode::Single);
	TEST_ASSERT_TRUE(w2b::chooseWriteMode(21, 23, RuleConfig::Oversize::Chunk) == WriteMode::Chunks);
	TEST_ASSERT_TRUE(w2b::chooseWriteMode(21, 23, RuleConfig::Oversize::LongWrite) == WriteMode::LongWrite);
	TEST_ASSERT_TRUE(w2b::chooseWriteMode(w2b::kMaxAttributeValue + 1, 255, RuleConfig::Oversize::LongWrite) == WriteMode::Drop);
	TEST_ASSERT_TRUE(w2b::chooseWriteMode(21, 23, RuleConfig::Oversize::Drop) == WriteMode::Drop);
}

static void test_status_json_layout()
{
	RelayCounters counters;
	counters.countRx(7);
	counters.countRx(9);
	counters.countTcpRx(8);
	counters.countTxWrite();

	TcpStatus::Clients clients{};
	clients[0] = "192.168.0.10:51234";
	const RelayStatus up("id0", "Relay 1", LinkState::Up, address("12:34:56:78:9a:bc"), 247, -61, 12, "", counters,
		TcpStatus(true, "", clients, 1));
	const RelayStatus waiting("id1", "Relay 2", LinkState::Waiting, std::nullopt, 0, 0, 0, "not found within 10 s of scanning", RelayCounters(),
		TcpStatus(false, "port 8899: no free socket", clients, 0));

	TEST_ASSERT_EQUAL_STRING("{\"id\":\"id0\",\"name\":\"Relay 1\",\"state\":\"up\",\"address\":\"12:34:56:78:9a:bc\",\"mtu\":247,\"rssi\":-61,"
		"\"up_s\":12,\"error\":\"\",\"rx_frames\":2,\"rx_bytes\":16,\"rx_dropped\":0,\"tcp_rx_bytes\":8,\"tcp_tx_bytes\":0,\"ble_tx_writes\":1,"
		"\"tx_dropped\":0,\"oversize_events\":0,\"reconnects\":0,\"listening\":true,\"tcp_error\":\"\",\"clients\":[\"192.168.0.10:51234\"]}",
		jsonOf(up).c_str());
	TEST_ASSERT_EQUAL_STRING("{\"id\":\"id1\",\"name\":\"Relay 2\",\"state\":\"waiting\",\"address\":\"\",\"mtu\":0,\"rssi\":0,\"up_s\":0,"
		"\"error\":\"not found within 10 s of scanning\",\"rx_frames\":0,\"rx_bytes\":0,\"rx_dropped\":0,\"tcp_rx_bytes\":0,\"tcp_tx_bytes\":0,"
		"\"ble_tx_writes\":0,\"tx_dropped\":0,\"oversize_events\":0,\"reconnects\":0,\"listening\":false,\"tcp_error\":\"port 8899: no free socket\","
		"\"clients\":[]}",
		jsonOf(waiting).c_str());
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_enabled_rules_are_grouped_by_device);
	RUN_TEST(test_a_device_wants_its_link_for_a_rule_that_is_always_up);
	RUN_TEST(test_the_backoff_follows_the_first_rule_its_last_wait_repeating);
	RUN_TEST(test_devices_compare_by_target_and_rules);
	RUN_TEST(test_find_takes_tx_and_rx_by_uuid_in_any_form);
	RUN_TEST(test_find_writes_as_the_characteristic_allows);
	RUN_TEST(test_find_subscribes_as_asked_and_offered);
	RUN_TEST(test_find_names_a_missing_characteristic_and_skips_a_channel_of_none);
	RUN_TEST(test_the_ring_keeps_packets_in_order);
	RUN_TEST(test_the_ring_splits_packets_across_its_end);
	RUN_TEST(test_the_ring_drops_the_oldest_whole_packets_for_a_new_one);
	RUN_TEST(test_readers_keep_their_own_place);
	RUN_TEST(test_a_reader_takes_a_packet_in_parts);
	RUN_TEST(test_a_reader_whose_packet_is_dropped_meanwhile_starts_at_the_oldest);
	RUN_TEST(test_the_ring_refuses_empty_and_too_long_packets_and_cuts_to_room);
	RUN_TEST(test_a_refusing_ring_keeps_its_packets_until_popped);
	RUN_TEST(test_an_open_packet_shows_once_closed);
	RUN_TEST(test_an_open_packet_wraps_and_is_read_in_chunks);
	RUN_TEST(test_client_data_closes_a_packet_after_the_silence);
	RUN_TEST(test_client_data_closes_at_once_at_a_chunk_or_when_full);
	RUN_TEST(test_write_mode_follows_the_mtu_and_the_oversize_policy);
	RUN_TEST(test_status_json_layout);

	return UNITY_END();
}
