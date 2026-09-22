#pragma once

#include "WebComponent.h"

namespace w2b
{

// The pages of web/, minified and gzipped at build time into WebAssets.h and served from flash with Content-Encoding: gzip and an
// ETag, so a browser's reload of the page it holds gets 304 and no body.
class WebPages : public WebComponent
{
public:
	// server: gets the page routes in setup()
	explicit WebPages(AsyncWebServer& server);

private:
	// GET /: web/index.html on the LAN address, web/setup.html on the setup access point. GET /setup: web/setup.html on both.
	void addRoutes() override;
};

}  // namespace w2b
