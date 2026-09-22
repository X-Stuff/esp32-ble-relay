#include "MdnsService.h"

#include <ESPmDNS.h>

#include "Config.h"
#include "Logger.h"

namespace w2b
{

namespace
{

constexpr const char* kTag = "mdns";

}  // namespace

MdnsService::MdnsService(const Config& config, uint16_t httpPort)
	: config(config), httpPort(httpPort)
{
}

void MdnsService::setup()
{
	const DeviceConfig device = config.device();
	const char* hostname = device.hostname().c_str();

	if (!MDNS.begin(hostname))
	{
		Logger::error(kTag, "starting mDNS as %s.local failed", hostname);
		return;
	}

	if (!MDNS.addService("http", "tcp", httpPort))
	{
		Logger::error(kTag, "announcing _http._tcp on port %u failed", (unsigned)httpPort);
		return;
	}

	Logger::info(kTag, "answering as %s.local, _http._tcp on port %u", hostname, (unsigned)httpPort);
}

}  // namespace w2b
