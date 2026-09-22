// Forward declarations of the ArduinoJson types headers use, so only .cpp files include the library. Namespace.hpp names the
// library's versioned inline namespace, so they match the real classes.
#pragma once

#include <ArduinoJson/Namespace.hpp>

ARDUINOJSON_BEGIN_PUBLIC_NAMESPACE
class JsonDocument;
class JsonObject;
class JsonObjectConst;
class JsonVariant;
ARDUINOJSON_END_PUBLIC_NAMESPACE

namespace w2b
{

// ArduinoJson's document
using ArduinoJson::JsonDocument;

// ArduinoJson's object
using ArduinoJson::JsonObject;

// ArduinoJson's read-only object
using ArduinoJson::JsonObjectConst;

// ArduinoJson's value
using ArduinoJson::JsonVariant;

}  // namespace w2b
