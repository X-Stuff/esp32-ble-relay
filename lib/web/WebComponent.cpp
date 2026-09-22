#include "WebComponent.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <esp_heap_caps.h>

namespace w2b
{

namespace
{

constexpr const char* kJsonType = "application/json";

constexpr const char* kApiPrefix = "/api/";

// a text answer takes at most this share of the largest free heap block; beyond it, it streams
constexpr size_t kHeapShare = 4;

// JSON serialized from the document it holds, again for each piece of the web server's send buffer, from the bytes sent so far on
class JsonDocumentResponse : public AsyncAbstractResponse
{
public:
	JsonDocumentResponse(int code, JsonDocument&& json, size_t length, bool pretty)
		: doc(std::move(json)), pretty(pretty)
	{
		_code = code;
		_contentType = kJsonType;
		_contentLength = length;
	}

	bool _sourceValid() const override
	{
		return true;
	}

	size_t _fillBuffer(uint8_t* data, size_t len) override
	{
		ChunkPrint piece(data, _sentLength, len);

		if (pretty)
		{
			serializeJsonPretty(doc, piece);
		}
		else
		{
			serializeJson(doc, piece);
		}

		return piece.written();
	}

private:
	JsonDocument doc;

	bool pretty;
};

// JSON serialized in one call into a buffer of its measured length, which the response sends from; without the buffer, no answer
class JsonBufferResponse : public AsyncAbstractResponse
{
public:
	JsonBufferResponse(int code, const JsonDocument& doc, size_t length, bool pretty)
		: text(new (std::nothrow) char[length + 1])
	{
		_code = code;
		_contentType = kJsonType;
		_contentLength = length;

		if (text == nullptr)
		{
			return;
		}

		if (pretty)
		{
			serializeJsonPretty(doc, text.get(), length + 1);
		}
		else
		{
			serializeJson(doc, text.get(), length + 1);
		}
	}

	bool _sourceValid() const override
	{
		return text != nullptr;
	}

	size_t _fillBuffer(uint8_t* data, size_t len) override
	{
		const size_t piece = std::min(len, _contentLength - sent);
		memcpy(data, text.get() + sent, piece);
		sent += piece;

		return piece;
	}

private:
	std::unique_ptr<char[]> text;

	// bytes given to the web server so far
	size_t sent = 0;
};

}  // namespace

WebComponent::WebComponent(AsyncWebServer& server)
	: webServer(server)
{
}

void WebComponent::setup()
{
	addRoutes();
}

void WebComponent::update(uint32_t /*dtMs*/)
{
}

AsyncWebServerResponse* WebComponent::makeJsonResponse(int code, JsonDocument&& doc, bool pretty)
{
	const size_t length = pretty ? measureJsonPretty(doc) : measureJson(doc);

	// streamed when long, and when the heap is short of one block for it: slower then, but no failed allocation
	if (length > kMaxTextJson || length > heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / kHeapShare)
	{
		return new JsonDocumentResponse(code, std::move(doc), length, pretty);
	}

	return new JsonBufferResponse(code, doc, length, pretty);
}

void WebComponent::sendJson(AsyncWebServerRequest* request, int code, JsonDocument&& doc)
{
	const bool pretty = false;
	request->send(makeJsonResponse(code, std::move(doc), pretty));
}

void WebComponent::sendError(AsyncWebServerRequest* request, int code, const std::string& error)
{
	JsonDocument doc;
	doc["error"] = error;
	sendJson(request, code, std::move(doc));
}

bool WebComponent::isOnAccessPoint(AsyncWebServerRequest* request)
{
	return ON_AP_FILTER(request);
}

bool WebComponent::refuseOnAccessPoint(AsyncWebServerRequest* request)
{
	if (!isOnAccessPoint(request))
	{
		return false;
	}

	sendError(request, 403, "only on the LAN address; the setup network is open to anyone in range");

	return true;
}

bool WebComponent::answerUnknownApi(AsyncWebServerRequest* request)
{
	if (!request->url().startsWith(kApiPrefix))
	{
		return false;
	}

	const bool withBody = request->method() == AsyncWebRequestMethod::HTTP_POST || request->method() == AsyncWebRequestMethod::HTTP_PUT;

	if (withBody && !request->contentType().equalsIgnoreCase(kJsonType))
	{
		sendError(request, 415, "expected a JSON body, Content-Type: application/json");
		return true;
	}

	sendError(request, 404, "no such API path for this method");

	return true;
}

}  // namespace w2b
