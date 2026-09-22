#include "LogService.h"

#include <cstdio>
#include <string>

#include <ESPAsyncWebServer.h>
#include <esp_random.h>

#include "Logger.h"

namespace w2b
{

namespace
{

// Up to 19 digits, so the value fits in 64 bits.
constexpr size_t kMaxOffsetDigits = 19;

// A decimal byte offset and nothing else, so a typo is an error rather than the whole ring.
bool parseOffset(const String& text, uint64_t& offset)
{
	if (text.length() == 0 || text.length() > kMaxOffsetDigits)
	{
		return false;
	}

	offset = 0;

	for (size_t i = 0; i < text.length(); ++i)
	{
		const char c = text[i];

		if (c < '0' || c > '9')
		{
			return false;
		}

		offset = offset * 10 + static_cast<uint64_t>(c - '0');
	}

	return true;
}

}  // namespace

LogService::LogService(AsyncWebServer& server)
	: WebComponent(server)
{
}

void LogService::setup()
{
	bootId = esp_random();
	WebComponent::setup();
}

void LogService::addRoutes()
{
	server().on(AsyncURIMatcher::exact("/api/log"), HTTP_GET,
		[this](AsyncWebServerRequest* request)
		{
			handleLog(request);
		});
}

void LogService::handleLog(AsyncWebServerRequest* request) const
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	uint64_t since = 0;
	const AsyncWebParameter* param = request->getParam("since");

	if (param != nullptr && !parseOffset(param->value(), since))
	{
		sendError(request, 400, "since: expected a byte offset, as X-Log-Next gave it");
		return;
	}

	uint64_t from = 0;
	uint64_t end = 0;
	Logger::findRange(since, from, end);

	char boot[9];
	snprintf(boot, sizeof(boot), "%08x", (unsigned)bootId);

	// piece by piece into the web server's send buffer; lines the ring drops meanwhile are skipped
	AsyncWebServerResponse* response = request->beginChunkedResponse("text/plain; charset=utf-8",
		[this, offset = from, end](uint8_t* buffer, size_t room, size_t /*index*/) mutable
		{
			return Logger::copyRange(offset, end, reinterpret_cast<char*>(buffer), room);
		});

	response->addHeader("X-Log-From", std::to_string(from).c_str());
	response->addHeader("X-Log-Next", std::to_string(end).c_str());
	response->addHeader("X-Log-Boot", boot);
	request->send(response);
}

}  // namespace w2b
