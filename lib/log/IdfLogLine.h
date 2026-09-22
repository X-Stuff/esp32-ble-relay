#pragma once

#include "Logger.h"

namespace w2b
{

// Splits an ESP-IDF log line, "E (389) tag: message\n" (WiFi: "tag:message"), in place into level, tag and message; drops the IDF stamp.
// Cuts trailing CR/LF, ends the tag with a NUL; tag and message point into text. Another shape: all message, level info, tag "idf".
void splitIdfLogLine(char* text, LogLevel& level, const char*& tag, const char*& message);

}  // namespace w2b
