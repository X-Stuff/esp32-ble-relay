#pragma once

#include <optional>
#include <string>

#include "BleAddress.h"
#include "FixedString.h"
#include "JsonSerializable.h"

namespace w2b
{

// The BLE device of a rule, "target" in a rule: its address, or the name it advertises, or both. Has no valid default: the empty
// object exists only for fromJson.
class TargetConfig : public IJsonSerializable<TargetConfig>
{
public:
	// for people, at most 63 bytes
	using Name = FixedString<63>;

	// address: none to find the device by name; name: as advertised, empty for none
	TargetConfig(const std::optional<BleAddress>& address, const Name& name);

private:
	friend class SerializableFactory;

	TargetConfig() = default;

public:
	// the device's address; none: the device is the first one advertising name()
	inline const std::optional<BleAddress>& address() const { return peer; }

	// as advertised; empty: none
	inline const Name& name() const { return advertisedName; }

	// An address, a name or both; the name fits and holds no control characters.
	bool validate(std::string& error) const;

	// address ("" for none), name
	void toJson(JsonObject& json) const override;

	// Every key optional, "address" "" for none; keys the JSON leaves out keep their value.
	bool fromJson(const JsonObjectConst& json, std::string& error) override;

	// every field equal
	bool operator==(const TargetConfig& other) const;

private:
	bool readAddress(const JsonObjectConst& json, std::string& error);

	std::optional<BleAddress> peer;

	Name advertisedName;
};

}  // namespace w2b
