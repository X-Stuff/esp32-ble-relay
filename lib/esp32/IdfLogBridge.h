#pragma once

namespace w2b
{

class FreeRtosMutex;

// Routes ESP-IDF log output, with USE_ESP_IDF_LOG the Arduino log_x calls too, into the Logger. Call once, after Serial.begin().
// loggerMutex: the Logger's lock, global; a line from an ISR or from a task holding it goes to the previous output.
void installIdfLogBridge(const FreeRtosMutex& loggerMutex);

}  // namespace w2b
