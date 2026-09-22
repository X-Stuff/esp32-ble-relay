// The GATT characteristics of a rule inside its service, "tx" and "rx" in a rule: TX takes the TCP clients' bytes as writes, RX brings
// the device's indications or notifications back to them.
#pragma once

#include <cstdint>
#include <string>

#include "BleUuid.h"
#include "JsonSerializable.h"

namespace w2b
{

// How a channel finds its characteristic
enum class ChannelMode : uint8_t
{
	// the characteristic of the channel's uuid
	Uuid,

	// no characteristic: TX discards the TCP clients' bytes, RX sends them nothing
	None,
};

// The characteristic that takes the TCP clients' bytes. The default state is valid: None, with response.
class TxConfig : public IJsonSerializable<TxConfig>
{
public:
	// None, with response
	TxConfig() = default;

	// uuid: with mode Uuid; none with None
	TxConfig(ChannelMode mode, const BleUuid& uuid, bool writeWithResponse);

	// how the characteristic is found
	inline ChannelMode mode() const { return channelMode; }

	// with mode Uuid; none with None
	inline const BleUuid& uuid() const { return characteristic; }

	// writes with response when the characteristic offers it
	inline bool writeWithResponse() const { return withResponse; }

	// Mode Uuid needs a UUID, None none.
	bool validate(std::string& error) const;

	// mode, uuid ("" for none), write_with_response
	void toJson(JsonObject& json) const override;

	// Keys the JSON leaves out keep their value; mode: uuid, none.
	bool fromJson(const JsonObjectConst& json, std::string& error) override;

	// every field equal
	bool operator==(const TxConfig& other) const;

private:
	ChannelMode channelMode = ChannelMode::None;

	BleUuid characteristic;

	bool withResponse = true;
};

// The characteristic whose indications or notifications go to the TCP clients. The default state is valid: None, subscribe Auto.
class RxConfig : public IJsonSerializable<RxConfig>
{
public:
	// Which subscription the rule asks for
	enum class Subscribe : uint8_t
	{
		// indicate when the characteristic has the Indicate property, notify otherwise
		Auto,

		// indications, each confirmed by the stack
		Indicate,

		// notifications, unconfirmed
		Notify,
	};

	// None, subscribe Auto
	RxConfig() = default;

	// uuid: with mode Uuid; none with None
	RxConfig(ChannelMode mode, const BleUuid& uuid, Subscribe subscribe);

	// how the characteristic is found
	inline ChannelMode mode() const { return channelMode; }

	// with mode Uuid; none with None
	inline const BleUuid& uuid() const { return characteristic; }

	// the subscription asked for
	inline Subscribe subscribe() const { return subscription; }

	// Mode Uuid needs a UUID, None none.
	bool validate(std::string& error) const;

	// mode, uuid ("" for none), subscribe
	void toJson(JsonObject& json) const override;

	// Keys the JSON leaves out keep their value; mode: uuid, none; subscribe: auto, indicate, notify.
	bool fromJson(const JsonObjectConst& json, std::string& error) override;

	// every field equal
	bool operator==(const RxConfig& other) const;

private:
	ChannelMode channelMode = ChannelMode::None;

	BleUuid characteristic;

	Subscribe subscription = Subscribe::Auto;
};

}  // namespace w2b
