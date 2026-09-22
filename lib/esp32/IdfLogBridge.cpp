#include "IdfLogBridge.h"

#include <cstdarg>
#include <cstdio>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include "FreeRtosMutex.h"
#include "IdfLogLine.h"
#include "Logger.h"

namespace w2b
{

namespace
{

const FreeRtosMutex* targetMutex = nullptr;
vprintf_like_t previous = nullptr;

const char* idfLevelName(esp_log_level_t level)
{
	switch (level)
	{
		case ESP_LOG_NONE:    return "none";
		case ESP_LOG_ERROR:   return "error";
		case ESP_LOG_WARN:    return "warn";
		case ESP_LOG_INFO:    return "info";
		case ESP_LOG_DEBUG:   return "debug";
		case ESP_LOG_VERBOSE: return "verbose";
		default:              return "?";
	}
}

// esp_log calls this for every line, in the task that logs. The line is formatted once here and re-logged as "%s".
int forward(const char* format, va_list args)
{
	if (targetMutex == nullptr || xPortInIsrContext() || targetMutex->heldByCurrentTask())
	{
		return previous != nullptr ? previous(format, args) : 0;
	}

	char line[Logger::kMaxLine + 1];
	int length = vsnprintf(line, sizeof(line), format, args);

	LogLevel level = LogLevel::Info;
	const char* tag = nullptr;
	const char* message = nullptr;
	splitIdfLogLine(line, level, tag, message);

	if (*message != '\0')
	{
		Logger::log(level, tag, "%s", message);
	}

	return length;
}

// ESP-IDF filters the Arduino log_x calls again at runtime (default level error); raising the tag lets them through, the log shows the level.
void raiseArduinoTag()
{
#ifdef USE_ESP_IDF_LOG
	esp_log_level_set(ARDUHAL_ESP_LOG_TAG, ESP_LOG_VERBOSE);
	Logger::info("log", "ESP-IDF output goes through the logger; Arduino core logs under tag \"%s\", runtime level %s", ARDUHAL_ESP_LOG_TAG,
		idfLevelName(esp_log_level_get(ARDUHAL_ESP_LOG_TAG)));
#else
	Logger::info("log", "ESP-IDF output goes through the logger; Arduino core logs still print directly (USE_ESP_IDF_LOG not set)");
#endif
}

}  // namespace

void installIdfLogBridge(const FreeRtosMutex& loggerMutex)
{
	targetMutex = &loggerMutex;
	previous = esp_log_set_vprintf(forward);

	raiseArduinoTag();
}

}  // namespace w2b
