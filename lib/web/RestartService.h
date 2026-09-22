#pragma once

#include <atomic>
#include <cstdint>

#include "WebComponent.h"

namespace w2b
{

// Restarts the board kDelayMs after a request from POST /api/reboot (LAN address only) or from any module through schedule().
class RestartService : public WebComponent
{
public:
	// from the request to the restart, so the HTTP answer gets out first
	static constexpr uint32_t kDelayMs = 1000;

	// server: gets the reboot route in setup()
	explicit RestartService(AsyncWebServer& server);

	// restarts kDelayMs after schedule()
	void update(uint32_t dtMs) override;

	// Any task. reason: logged at the restart, must live for the rest of the run (a string literal); the first call wins.
	void schedule(const char* reason);

private:
	void addRoutes() override;

	// POST /api/reboot: 202 {restart_in_ms}, then the board restarts
	void handleReboot(AsyncWebServerRequest* request);

private:
	// the reason; null until a restart is requested
	std::atomic<const char*> requested{nullptr};

	// application thread only: time since the request
	uint32_t waitedMs = 0;
};

}  // namespace w2b
