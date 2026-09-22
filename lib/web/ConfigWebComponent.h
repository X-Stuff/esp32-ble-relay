#pragma once

#include "Config.h"
#include "WebComponent.h"

namespace w2b
{

// Base of the web modules whose requests change the configuration: each change goes to Config::submit(), the request gets the result,
// and the page follows the save with GET /api/config/save.
class ConfigWebComponent : public WebComponent
{
public:
	// config: the running configuration the requests read and change
	ConfigWebComponent(AsyncWebServer& server, Config& config);

protected:
	// the running configuration
	inline Config& config() const { return running; }

	// {"save": state}
	static void sendSaveState(AsyncWebServerRequest* request, int code, Config::SaveState state);

	// Submits the change and answers 202 {"save": "pending"}, 400 {"error"} when the edit refuses it or the result is invalid, 409 while
	// another change is pending. what: for the log; followUp may be empty.
	void submit(AsyncWebServerRequest* request, const char* what, const Config::Edit& edit, Config::FollowUp followUp) const;

private:
	Config& running;
};

}  // namespace w2b
