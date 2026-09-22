#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include <esp_partition.h>

#include "JsonForward.h"
#include "WebComponent.h"

class String;

namespace w2b
{

// Firmware update over HTTP into the other app slot, with rollback of an image that does not confirm itself. LAN address only.
// Writes the other app slot and the OTA data partition; restarts the board.
class OtaService : public WebComponent
{
public:
	// uptime after which a new image confirms itself; a reset before that makes the bootloader start the previous slot
	static constexpr uint32_t kConfirmAfterMs = 30000;

	// from a verified upload to the restart, so the answer gets out first
	static constexpr uint32_t kRestartDelayMs = 1000;

	// receive timeout of the upload connection, up from the server's 3 s: a 64 KB flash block erase holds the handler longer
	static constexpr uint32_t kRxTimeoutS = 20;

	// server: gets the upload routes in setup()
	explicit OtaService(AsyncWebServer& server);

	// logs both app slots and their states, adds the routes
	void setup() override;

	// confirms a new image after kConfirmAfterMs; restarts after a verified upload
	void update(uint32_t dtMs) override;

	// the app slots for /api/status: "running", "running_state", "other", "other_state"; "confirm_after_s", kConfirmAfterMs
	void toJson(JsonObject& json) const;

private:
	// How far the last upload got
	enum class UploadState : uint8_t
	{
		// no upload since boot
		Idle,

		// bytes arriving and written
		Receiving,

		// ESP-IDF verified the whole image; the other slot boots next
		Verified,

		// aborted; the running slot stays
		Failed,
	};

	// the state as /api/ota/progress names it
	static const char* nameOf(UploadState state);

	// the partition's label; "?" without a partition
	static const char* labelOf(const esp_partition_t* partition);

	// the slot's OTA state as /api/status names it; "none" without OTA data, as after a USB upload
	static const char* stateOf(const esp_partition_t* partition);

	// the running image still waits for its confirmation
	static bool runningPendingVerify();

	void addRoutes() override;

	// POST /api/ota, each piece of the multipart file (RESPONSE_STREAM_BUFFER_SIZE bytes at most), written as it arrives; the last one
	// has final set. The flash writes block the web server's task: the data is valid only inside the callback.
	void receive(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final);

	// first piece of an upload: takes the upload and starts Update; ?md5=<32 hex> is checked too when given
	bool start(AsyncWebServerRequest* request, const String& filename);

	// aborts Update and keeps the reason for /api/ota/progress
	void fail(const std::string& reason);

	// POST /api/ota, once the whole request has arrived: {slot, written, restart_in_ms} after a verified image, else 400 or 409
	void finish(AsyncWebServerRequest* request);

	// GET /api/ota/progress, polled by the page during an upload: {state, written, error?}
	void handleProgress(AsyncWebServerRequest* request) const;

	// reads both slots' states from the OTA data partition: at boot, after a confirm and after a verified upload
	void readStates();

	void waitImageToConfirm(uint32_t dtMs);

	// marks the running image valid, which cancels the rollback
	void confirmImage();

	void waitAndRestart(uint32_t dtMs);

private:
	// web server's task only: the request of the running upload; compared, never dereferenced
	AsyncWebServerRequest* owner = nullptr;

	// web server's task only
	UploadState uploadState = UploadState::Idle;

	// web server's task only: bytes of the file given to Update in the last upload
	size_t written = 0;

	// web server's task only: why the last upload failed
	std::string failure;

	// application thread only: time left until a new image confirms itself; zero when no image waits for it
	uint32_t pendingConfirmationMs = 0;

	// set by the web server's task after a verified upload, read by update()
	std::atomic<bool> restartRequested{false};

	// application thread only: time since the restart was requested
	uint32_t restartMs = 0;

	// the running slot's state as /api/status names it; written by both threads, read by the web server's task
	std::atomic<const char*> runningState{"none"};

	// the other slot's state, the same way
	std::atomic<const char*> otherState{"none"};
};

}  // namespace w2b
