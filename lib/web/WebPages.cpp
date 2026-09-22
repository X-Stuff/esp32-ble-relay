#include "WebPages.h"

#include <cstddef>
#include <cstdint>

#include <ESPAsyncWebServer.h>

// Generated into the build directory by scripts/web_assets.py before every build: one gzipped array per web/*.html, named after the file.
#include "WebAssets.h"

namespace w2b
{

namespace
{

// A browser that holds the page (its If-None-Match is the tag) gets 304, no body; else the page streamed from flash, no heap copy.
void sendPage(AsyncWebServerRequest* request, const uint8_t* page, size_t size, const char* etag)
{
	if (request->header("If-None-Match") == etag)
	{
		request->send(304);
		return;
	}

	// every browser takes gzip; curl needs --compressed
	AsyncWebServerResponse* response = request->beginResponse(200, "text/html", page, size);
	response->addHeader("Content-Encoding", "gzip");
	response->addHeader("ETag", etag);
	response->addHeader("Cache-Control", "no-cache");
	request->send(response);
}

void sendIndex(AsyncWebServerRequest* request)
{
	sendPage(request, webassets::indexHtml, sizeof(webassets::indexHtml), webassets::indexHtmlEtag);
}

void sendSetup(AsyncWebServerRequest* request)
{
	sendPage(request, webassets::setupHtml, sizeof(webassets::setupHtml), webassets::setupHtmlEtag);
}

}  // namespace

WebPages::WebPages(AsyncWebServer& server)
	: WebComponent(server)
{
}

void WebPages::addRoutes()
{
	server().on(AsyncURIMatcher::exact("/"), HTTP_GET,
		[](AsyncWebServerRequest* request)
		{
			if (isOnAccessPoint(request))
			{
				sendSetup(request);
			}
			else
			{
				sendIndex(request);
			}
		});

	server().on(AsyncURIMatcher::exact("/setup"), HTTP_GET, sendSetup);
}

}  // namespace w2b
