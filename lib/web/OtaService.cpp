#include "OtaService.h"

#include <utility>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <esp_ota_ops.h>

#include "BoardInfo.h"
#include "Logger.h"

// Overrides the core's weak false, which confirms a new image before setup(); here, so the linker takes it along with OtaService.
extern "C" bool verifyRollbackLater()
{
	return true;
}

namespace w2b
{

namespace
{

constexpr const char* kTag = "ota";

}  // namespace

OtaService::OtaService(AsyncWebServer& server)
	: WebComponent(server)
{
}

void OtaService::setup()
{
	const esp_partition_t* running = BoardInfo::runningApp();
	const esp_partition_t* other = BoardInfo::updateApp();

	readStates();
	Logger::info(kTag, "current app slot %s (%s), OTA app slot %s (%s)", labelOf(running), runningState.load(), labelOf(other), otherState.load());

	if (runningPendingVerify())
	{
		Logger::info(kTag, "new image: confirmed after %u s without a reset, otherwise the bootloader returns to slot %s",
			(unsigned)(kConfirmAfterMs / 1000), labelOf(other));

		pendingConfirmationMs = kConfirmAfterMs;
	}

	esp_ota_img_states_t otherState = ESP_OTA_IMG_UNDEFINED;

	if (other != nullptr && esp_ota_get_state_partition(other, &otherState) == ESP_OK &&
		(otherState == ESP_OTA_IMG_ABORTED || otherState == ESP_OTA_IMG_INVALID))
	{
		Logger::warn(kTag, "image in slot '%s' did not confirm itself; the bootloader rolled back from it", labelOf(other));
	}

	WebComponent::setup();
}

void OtaService::update(uint32_t dtMs)
{
	if (pendingConfirmationMs > 0)
	{
		waitImageToConfirm(dtMs);
	}

	if (restartRequested)
	{
		waitAndRestart(dtMs);
	}
}

void OtaService::toJson(JsonObject& json) const
{
	const esp_partition_t* running = BoardInfo::runningApp();
	const esp_partition_t* other = BoardInfo::updateApp();

	json["running"] = labelOf(running);
	json["running_state"] = runningState.load();
	json["other"] = labelOf(other);
	json["other_state"] = otherState.load();
	json["confirm_after_s"] = kConfirmAfterMs / 1000;
}

const char* OtaService::nameOf(UploadState state)
{
	switch (state)
	{
		case UploadState::Idle:      return "idle";
		case UploadState::Receiving: return "receiving";
		case UploadState::Verified:  return "verified";
		case UploadState::Failed:    return "failed";
	}

	return "?";
}

const char* OtaService::labelOf(const esp_partition_t* partition)
{
	return partition != nullptr ? partition->label : "?";
}

const char* OtaService::stateOf(const esp_partition_t* partition)
{
	esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;

	if (partition == nullptr || esp_ota_get_state_partition(partition, &state) != ESP_OK)
	{
		return "none";
	}

	switch (state)
	{
		case ESP_OTA_IMG_NEW:            return "new";
		case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
		case ESP_OTA_IMG_VALID:          return "valid";
		case ESP_OTA_IMG_INVALID:        return "invalid";
		case ESP_OTA_IMG_ABORTED:        return "aborted";
		case ESP_OTA_IMG_UNDEFINED:      return "undefined";
	}

	return "?";
}

bool OtaService::runningPendingVerify()
{
	esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;

	return esp_ota_get_state_partition(BoardInfo::runningApp(), &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
}

void OtaService::addRoutes()
{
	server().on(AsyncURIMatcher::exact("/api/ota"), HTTP_POST,
		[this](AsyncWebServerRequest* request)
		{
			finish(request);
		},
		[this](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final)
		{
			receive(request, filename, index, data, len, final);
		});

	server().on(AsyncURIMatcher::exact("/api/ota/progress"), HTTP_GET,
		[this](AsyncWebServerRequest* request)
		{
			handleProgress(request);
		});
}

void OtaService::receive(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final)
{
	// nothing of an upload on the setup access point is written; finish() answers it 403
	if (isOnAccessPoint(request))
	{
		return;
	}

	if (index == 0 && !start(request, filename))
	{
		return;
	}

	if (request != owner || uploadState != UploadState::Receiving)
	{
		return;
	}

	if (Update.write(data, len) != len)
	{
		fail(std::string("writing failed: ") + Update.errorString());
		return;
	}

	written += len;

	if (!final)
	{
		return;
	}

	if (!Update.end(true))
	{
		fail(std::string("the image did not verify: ") + Update.errorString());
		return;
	}

	uploadState = UploadState::Verified;
	readStates();
}

// A second upload while one runs is left without owner; finish() answers it 409.
bool OtaService::start(AsyncWebServerRequest* request, const String& filename)
{
	if (owner != nullptr)
	{
		return false;
	}

	owner = request;
	uploadState = UploadState::Receiving;
	written = 0;
	failure.clear();
	request->client()->setRxTimeout(kRxTimeoutS);

	// Lost before the end: the update is abandoned. Lost after verification: the boot slot has switched already, so restart as planned.
	request->onDisconnect(
		[this, request]
		{
			if (owner != request)
			{
				return;
			}

			owner = nullptr;

			if (uploadState == UploadState::Verified)
			{
				Logger::info(kTag, "connection lost after the image verified; restart in %u s", (unsigned)(kRestartDelayMs / 1000));
				restartRequested = true;
			}
			else if (uploadState == UploadState::Receiving)
			{
				fail("the upload connection was lost");
			}
		});

	if (runningPendingVerify())
	{
		fail("the running firmware has not confirmed itself yet; try again after " + std::to_string(kConfirmAfterMs / 1000) + " s of uptime");
		return false;
	}

	if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
	{
		fail(std::string("starting failed: ") + Update.errorString());
		return false;
	}

	const AsyncWebParameter* md5 = request->getParam("md5");

	if (md5 != nullptr && !Update.setMD5(md5->value().c_str()))
	{
		fail("md5: expected 32 hex digits");
		return false;
	}

	Logger::info(kTag, "receiving %s into OTA app slot %s%s", filename.c_str(), labelOf(BoardInfo::updateApp()),
		md5 != nullptr ? ", md5 checked" : "");

	return true;
}

void OtaService::fail(const std::string& reason)
{
	failure = reason;
	uploadState = UploadState::Failed;

	if (Update.isRunning())
	{
		Update.abort();
	}

	Logger::warn(kTag, "update rejected after %u bytes: %s; the running slot stays", (unsigned)written, reason.c_str());
}

void OtaService::finish(AsyncWebServerRequest* request)
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	if (request != owner)
	{
		const bool busy = owner != nullptr;
		sendError(request, busy ? 409 : 400, busy ? "another firmware upload is running" : "no firmware file in the request (a multipart file field)");
		return;
	}

	owner = nullptr;

	if (uploadState == UploadState::Receiving)
	{
		fail("the upload ended before the image was complete");
	}

	if (uploadState != UploadState::Verified)
	{
		sendError(request, 400, failure);
		return;
	}

	const char* slot = labelOf(esp_ota_get_boot_partition());

	JsonDocument doc;
	doc["slot"] = slot;
	doc["written"] = written;
	doc["restart_in_ms"] = kRestartDelayMs;
	sendJson(request, 200, std::move(doc));

	Logger::info(kTag, "image of %u bytes verified: slot %s boots next, restart in %u s", (unsigned)written, slot, (unsigned)(kRestartDelayMs / 1000));
	restartRequested = true;
}

void OtaService::handleProgress(AsyncWebServerRequest* request) const
{
	JsonDocument doc;
	doc["state"] = nameOf(uploadState);
	doc["written"] = written;

	if (!failure.empty())
	{
		doc["error"] = failure;
	}

	sendJson(request, 200, std::move(doc));
}

// Flash reads, and an ESP-IDF error line each without OTA data: only when a state can have changed, never per status.
void OtaService::readStates()
{
	runningState = stateOf(BoardInfo::runningApp());
	otherState = stateOf(BoardInfo::updateApp());
}

void OtaService::waitImageToConfirm(uint32_t dtMs)
{
	if (pendingConfirmationMs > dtMs)
	{
		pendingConfirmationMs -= dtMs;
		return;
	}

	pendingConfirmationMs = 0;
	confirmImage();
}

// Only a pending image needs it; a USB upload or an image confirmed on an earlier boot has nothing to confirm.
void OtaService::confirmImage()
{
	if (!runningPendingVerify())
	{
		return;
	}

	const esp_partition_t* running = BoardInfo::runningApp();

	if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK)
	{
		Logger::error(kTag, "confirming the image in slot %s failed", labelOf(running));
		return;
	}

	Logger::info(kTag, "image in slot %s confirmed after %u s: it stays", labelOf(running), (unsigned)(kConfirmAfterMs / 1000));
	readStates();
}

void OtaService::waitAndRestart(uint32_t dtMs)
{
	restartMs += dtMs;

	if (restartMs >= kRestartDelayMs)
	{
		Logger::info(kTag, "restarting into the new firmware");
		Serial.flush();
		ESP.restart();
	}
}

}  // namespace w2b
