#include "ConfigWebComponent.h"

#include <string>
#include <utility>

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

namespace w2b
{

ConfigWebComponent::ConfigWebComponent(AsyncWebServer& server, Config& config)
	: WebComponent(server), running(config)
{
}

void ConfigWebComponent::sendSaveState(AsyncWebServerRequest* request, int code, Config::SaveState state)
{
	JsonDocument doc;
	doc["save"] = Config::nameOf(state);
	sendJson(request, code, std::move(doc));
}

void ConfigWebComponent::submit(AsyncWebServerRequest* request, const char* what, const Config::Edit& edit, Config::FollowUp followUp) const
{
	std::string error;

	switch (running.submit(what, edit, std::move(followUp), error))
	{
		case Config::ChangeResult::Accepted:
		{
			sendSaveState(request, 202, Config::SaveState::Pending);
			return;
		}
		case Config::ChangeResult::Invalid:
		{
			sendError(request, 400, error);
			return;
		}
		case Config::ChangeResult::Busy:
		{
			sendError(request, 409, error);
			return;
		}
	}

	sendError(request, 500, "unknown result");
}

}  // namespace w2b
