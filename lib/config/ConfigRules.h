// Value rules shared by the config classes' validate().
#pragma once

#include <cstdint>

#include "BleUuid.h"

namespace w2b
{

// An RFC 1123 label: 1..63 letters, digits, hyphens, no hyphen at either end.
bool isValidHostname(const char* name);

// Empty (an open network), 8..63 printable ASCII characters, or 64 hex digits.
bool isValidWifiPassword(const char* password);

// A run of ones followed by zeros, not all zeros.
bool isValidSubnetMask(uint32_t mask);

// Reads a strict dotted quad: four decimal parts of one to three digits, each at most 255, nothing around; false for anything else.
bool parseIPv4(const char* text, uint32_t& address);

// A rule id, part of the rule's URLs: 1..32 letters, digits, hyphens or underscores.
bool isValidRuleId(const char* id);

// Text for people: no control characters.
bool isValidLabel(const char* text);

}  // namespace w2b
