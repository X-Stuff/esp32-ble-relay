#include "RuleService.h"

#include <cstdio>
#include <cstring>
#include <utility>

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "RuleConfig.h"

namespace w2b
{

namespace
{

constexpr const char* kRulesPath = "/api/rules";

// a rule is about 1 KB at most, indented as its export writes it
constexpr size_t kMaxBodyBytes = 2048;

// "/api/rules/id0/export": id "id0", action "export"; "/api/rules/id0": action empty.
void splitPath(const AsyncWebServerRequest* request, std::string& id, std::string& action)
{
	const std::string path = request->url().c_str();
	const size_t start = std::strlen(kRulesPath) + 1;
	const std::string tail = path.size() > start ? path.substr(start) : std::string();
	const size_t slash = tail.find('/');

	id = tail.substr(0, slash);
	action = slash == std::string::npos ? std::string() : tail.substr(slash + 1);
}

}  // namespace

RuleService::RuleService(AsyncWebServer& server, Config& config)
	: ConfigWebComponent(server, config)
{
}

bool RuleService::readRule(JsonVariant& json, RuleConfig& rule, std::string& error)
{
	if (!json.is<JsonObjectConst>())
	{
		error = "not a JSON object";
		return false;
	}

	return rule.fromJson(json.as<JsonObjectConst>(), error);
}

// /api/rules exactly and everything below it, one handler per method: the library's HTTP_* names can clash with ESP-IDF's.
void RuleService::addRoutes()
{
	server().on(AsyncURIMatcher::exact(kRulesPath), HTTP_GET,
		[this](AsyncWebServerRequest* request)
		{
			handleList(request);
		});

	server()
		.on(AsyncURIMatcher::exact(kRulesPath), HTTP_POST,
			[this](AsyncWebServerRequest* request, JsonVariant& json)
			{
				handleAdd(request, json);
			})
		.setMaxContentLength(kMaxBodyBytes);

	server()
		.on(AsyncURIMatcher::dir(kRulesPath), HTTP_PUT,
			[this](AsyncWebServerRequest* request, JsonVariant& json)
			{
				handleReplace(request, json);
			})
		.setMaxContentLength(kMaxBodyBytes);

	server().on(AsyncURIMatcher::dir(kRulesPath), HTTP_DELETE,
		[this](AsyncWebServerRequest* request)
		{
			handleRemove(request);
		});

	server().on(AsyncURIMatcher::dir(kRulesPath), HTTP_GET,
		[this](AsyncWebServerRequest* request)
		{
			handleExport(request);
		});
}

void RuleService::handleList(AsyncWebServerRequest* request) const
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	JsonDocument doc;
	JsonArray list = doc["rules"].to<JsonArray>();

	config().read([&list](const ConfigData& data)
		{
			for (const RuleConfig& rule : data.rules())
			{
				JsonObject item = list.add<JsonObject>();
				rule.toJson(item);
			}
		});

	JsonObject defaults = doc["defaults"].to<JsonObject>();
	SerializableFactory::createEmpty<RuleConfig>().toJson(defaults);

	JsonObject limits = doc["limits"].to<JsonObject>();
	limits["enabled"] = ConfigData::kMaxActiveRules;
	limits["clients"] = RuleConfig::kMaxClients;
	limits["coalesce_ms"] = RuleConfig::kMaxCoalesceMs;
	limits["idle_s"] = RuleConfig::kMaxIdleS;
	limits["web_port"] = RuleConfig::kWebPort;

	sendJson(request, 200, std::move(doc));
}

void RuleService::handleAdd(AsyncWebServerRequest* request, JsonVariant& json)
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	RuleConfig rule = SerializableFactory::createEmpty<RuleConfig>();
	std::string error;

	if (!readRule(json, rule, error))
	{
		sendError(request, 400, error);
		return;
	}

	// the edit names the new id in it
	char what[64] = "rule added";

	submit(request, what,
		[&rule, &what](ConfigData& newConfig, std::string& /*reason*/)
		{
			std::snprintf(what, sizeof(what), "rule %s added", newConfig.addRule(rule).c_str());
			return true;
		},
		nullptr);
}

void RuleService::handleReplace(AsyncWebServerRequest* request, JsonVariant& json)
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	std::string id;
	RuleConfig stored = SerializableFactory::createEmpty<RuleConfig>();

	if (!findRuleOf(request, "", id, stored))
	{
		return;
	}

	RuleConfig rule = SerializableFactory::createEmpty<RuleConfig>();
	std::string error;

	if (!readRule(json, rule, error))
	{
		sendError(request, 400, error);
		return;
	}

	if (!rule.id().empty() && rule.id() != id.c_str())
	{
		sendError(request, 400, "id: \"" + std::string(rule.id().c_str()) + "\" is not the id of the rule in the path, \"" + id + "\"");
		return;
	}

	char what[64];
	std::snprintf(what, sizeof(what), "rule %s changed", id.c_str());

	submit(request, what,
		[&rule, &id](ConfigData& newConfig, std::string& reason)
		{
			return newConfig.replaceRule(id.c_str(), rule, reason);
		},
		nullptr);
}

void RuleService::handleRemove(AsyncWebServerRequest* request)
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	std::string id;
	RuleConfig rule = SerializableFactory::createEmpty<RuleConfig>();

	if (!findRuleOf(request, "", id, rule))
	{
		return;
	}

	char what[64];
	std::snprintf(what, sizeof(what), "rule %s removed", id.c_str());

	submit(request, what,
		[&id](ConfigData& newConfig, std::string& reason)
		{
			return newConfig.removeRule(id.c_str(), reason);
		},
		nullptr);
}

void RuleService::handleExport(AsyncWebServerRequest* request) const
{
	if (refuseOnAccessPoint(request))
	{
		return;
	}

	std::string id;
	RuleConfig rule = SerializableFactory::createEmpty<RuleConfig>();

	if (!findRuleOf(request, "export", id, rule))
	{
		return;
	}

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	rule.toJson(root);

	const std::string disposition = "attachment; filename=\"" + std::string(config().device().hostname().c_str()) + "-rule-" + id + ".json\"";
	const bool pretty = true;
	AsyncWebServerResponse* response = makeJsonResponse(200, std::move(doc), pretty);
	response->addHeader("Content-Disposition", disposition.c_str());
	request->send(response);
}

bool RuleService::findRuleOf(AsyncWebServerRequest* request, const char* action, std::string& id, RuleConfig& rule) const
{
	std::string actual;
	splitPath(request, id, actual);

	if (actual != action)
	{
		sendError(request, 404, *action != '\0' ? std::string("expected /api/rules/{id}/") + action : std::string("expected /api/rules/{id}"));
		return false;
	}

	if (!config().findRule(id.c_str(), rule))
	{
		sendError(request, 404, "no rule \"" + id + "\"");
		return false;
	}

	return true;
}

}  // namespace w2b
