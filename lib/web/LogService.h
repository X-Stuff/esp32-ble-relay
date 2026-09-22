#pragma once

#include <cstdint>

#include "WebComponent.h"

namespace w2b
{

// The Logger's RAM ring over HTTP as plain text, from a byte offset on. LAN address only: the log names networks and addresses.
class LogService : public WebComponent
{
public:
	// server: gets the log route in setup()
	explicit LogService(AsyncWebServer& server);

	// draws the boot id, adds the routes
	void setup() override;

private:
	void addRoutes() override;

	// GET /api/log?since=N: the ring from byte offset N on, all of it without N, streamed. X-Log-From: offset of the first byte, above N
	// when the ring has dropped the lines between; X-Log-Next: offset to ask for next; X-Log-Boot: drawn at every boot, as offsets restart.
	void handleLog(AsyncWebServerRequest* request) const;

private:
	// X-Log-Boot, drawn in setup(): the radio is on by then, so the hardware random generator has its entropy
	uint32_t bootId = 0;
};

}  // namespace w2b
