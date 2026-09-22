#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "Component.h"
#include "JsonForward.h"

class AsyncWebServer;
class AsyncWebServerRequest;
class AsyncWebServerResponse;

namespace w2b
{

// Base of the modules that serve pages and the HTTP API: setup() adds the routes, update() takes the work handlers hand over, since
// handlers run on the web server's task and never block. Routes match exactly: "/api/wifi" would otherwise take "/api/wifi/scan".
class WebComponent : public Component
{
public:
	// A JSON answer up to this is serialized once into a buffer of its size, held until sent; a longer one, or any while the heap has
	// no block to spare for it, holds its document instead and serializes each piece as it goes out: time, no buffer.
	static constexpr size_t kMaxTextJson = 4096;

	// server: the one web server; its owner starts it after every module's setup()
	explicit WebComponent(AsyncWebServer& server);

	// Adds the routes; a module that overrides it calls it too.
	void setup() override;

	// nothing; a module with work for the application thread overrides it
	void update(uint32_t dtMs) override;

protected:
	// the web server
	inline AsyncWebServer& server() const { return webServer; }

	// the request came in on the setup access point's address, not the LAN one
	static bool isOnAccessPoint(AsyncWebServerRequest* request);

	// Answers 403 when the request came in on the setup access point, open to anyone in range, and returns true, so the handler of a
	// path the LAN address alone serves returns at once.
	static bool refuseOnAccessPoint(AsyncWebServerRequest* request);

	// The answer with doc as its JSON body, compact or pretty: up to kMaxTextJson serialized into a buffer of its size, a longer one
	// serialized from doc piece by piece as it goes out, holding doc until then.
	static AsyncWebServerResponse* makeJsonResponse(int code, JsonDocument&& doc, bool pretty);

	// sends doc as compact JSON, through makeJsonResponse()
	static void sendJson(AsyncWebServerRequest* request, int code, JsonDocument&& doc);

	// {"error": error}
	static void sendError(AsyncWebServerRequest* request, int code, const std::string& error);

	// A request no route took, on a path under /api/: answers 415 for a POST or PUT without Content-Type application/json, which the
	// JSON handlers leave, 404 otherwise, and returns true; false, nothing sent, for any other path.
	static bool answerUnknownApi(AsyncWebServerRequest* request);

	// Registers the module's routes on server(); setup() calls it once.
	virtual void addRoutes() = 0;

private:
	AsyncWebServer& webServer;
};

}  // namespace w2b
