#pragma once

#include <string>

#include "ConfigWebComponent.h"
#include "JsonForward.h"

namespace w2b
{

class RuleConfig;

// The relay rules over HTTP: list, add, replace, remove, export; LAN address only. The rules are stored, not run yet. Writes
// /config.json after every accepted change.
class RuleService : public ConfigWebComponent
{
public:
	// config: holds the rules
	RuleService(AsyncWebServer& server, Config& config);

private:
	// "not a JSON object" or the rule's own "field: reason"
	static bool readRule(JsonVariant& json, RuleConfig& rule, std::string& error);

	void addRoutes() override;

	// GET /api/rules: {"rules": [...], "defaults": a new rule's keys, "limits": {"enabled", "clients", "coalesce_ms", "idle_s", "web_port"}},
	// so the page repeats none of RuleConfig's and ConfigData's values
	void handleList(AsyncWebServerRequest* request) const;

	// POST /api/rules: a new rule under the first free id; target (address or name) and service_uuid required, other keys their defaults
	void handleAdd(AsyncWebServerRequest* request, JsonVariant& json);

	// PUT /api/rules/{id}: replaces the rule in its place; keys left out take their defaults; a body's id must be {id}
	void handleReplace(AsyncWebServerRequest* request, JsonVariant& json);

	// DELETE /api/rules/{id}
	void handleRemove(AsyncWebServerRequest* request);

	// GET /api/rules/{id}/export: the rule alone, a valid POST body, as the download <hostname>-rule-<id>.json
	void handleExport(AsyncWebServerRequest* request) const;

	// The {id} of /api/rules/{id}[/action] into id, a copy of its rule into rule; false after answering 404 for another action or an
	// unknown id.
	bool findRuleOf(AsyncWebServerRequest* request, const char* action, std::string& id, RuleConfig& rule) const;
};

}  // namespace w2b
