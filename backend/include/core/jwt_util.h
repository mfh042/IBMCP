#pragma once

#include <json/json.h>
#include <optional>
#include <string>

namespace ibmcp::jwt {

std::string createToken(const std::string &sub, const Json::Value &extra = Json::Value());

std::optional<Json::Value> verifyToken(const std::string &token);

}  // namespace ibmcp::jwt