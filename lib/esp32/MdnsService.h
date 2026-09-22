#pragma once

#include <cstdint>

namespace w2b
{

class Config;

// mDNS: answers as <hostname>.local and announces the web page as _http._tcp, on the station and the access point alike. Sends mDNS
// announcements and answers; a new hostname takes effect at the next boot. Firmware only.
class MdnsService
{
public:
	// config: holds the hostname; httpPort: the web server's, announced with _http._tcp
	MdnsService(const Config& config, uint16_t httpPort);

	// Starts mDNS; after the WiFi start, since the mdns component needs its network interfaces.
	void setup();

private:
	const Config& config;

	uint16_t httpPort;
};

}  // namespace w2b
