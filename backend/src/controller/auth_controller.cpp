#include "controller/auth_controller.h"

#include <algorithm>
#include <chrono>

#include "controller/common.h"
#include "core/auth.h"
#include "core/auth_redis.h"
#include "core/errors.h"
#include "core/jwt_util.h"
#include "core/password_util.h"
#include "core/result.h"
#include "db/db.h"
#include "util/json_util.h"

using namespace drogon;

namespace ibmcp {

namespace {

int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string pgArrayLiteral(const std::vector<std::string> &items) {
    std::string out = "{";
    bool first = true;
    for (const auto &s : items) {
        if (!first) out += ",";
        first = false;
        out += "\"";
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += "\"";
    }
    out += "}";
    return out;
}

std::vector<std::string> jsonStrVec(const Json::Value &v) {
    std::vector<std::string> out;
    if (v.isArray()) {
        for (const auto &e : v) out.push_back(e.asString());
    }
    return out;
}

void requireAdmin(HttpRequestPtr req) {
    if (!auth::hasRole(req, "admin"))
        throw BizError(err::FORBIDDEN, "admin required");
}

}  // namespace

Task<HttpResponsePtr> AuthController::login(HttpRequestPtr req) {
    auto j = req->getJsonObject();
    if (!j) throw BizError(err::VALIDATION_FAILED, "request body required");
    const auto &body = *j;
    std::string username = body.get("username", "").asString();
    std::string password = body.get("password", "").asString();
    if (username.empty() || password.empty())
        throw BizError(1001, "invalid credentials");

    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT id, username, display_name, password_hash, array_to_json(roles) AS roles, enabled "
        "FROM sys_users WHERE username = $1",
        username);
    if (rows.empty() || !rows[0]["enabled"].as<bool>())
        throw BizError(1001, "invalid credentials");
    const auto &row = rows[0];
    if (!password::verify(password, row["password_hash"].as<std::string>()))
        throw BizError(1001, "invalid credentials");

    Json::Value extra(Json::objectValue);
    extra["username"] = row["username"].as<std::string>();
    extra["roles"] = util::rowToJson(row)["roles"];
    std::string sub = std::to_string(row["id"].as<int64_t>());
    std::string token = jwt::createToken(sub, extra);

    Json::Value refreshExtra(Json::objectValue);
    refreshExtra["username"] = row["username"].as<std::string>();
    refreshExtra["scope"] = "refresh";
    std::string refresh = jwt::createToken(sub, refreshExtra);

    Json::Value data(Json::objectValue);
    data["token"] = token;
    data["refresh_token"] = refresh;
    data["id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
    data["username"] = row["username"].as<std::string>();
    auto dn = row["display_name"];
    data["display_name"] =
        dn.isNull() || dn.as<std::string>().empty() ? row["username"].as<std::string>()
                                                    : dn.as<std::string>();
    data["roles"] = util::rowToJson(row)["roles"];
    co_return result::ok(data);
}

Task<HttpResponsePtr> AuthController::logout(HttpRequestPtr req) {
    auth::requireAuth(req);
    int64_t now = nowEpoch();

    // 拉黑当前 access token（按剩余有效期 TTL）。
    std::string authHeader = req->getHeader("Authorization");
    static const std::string prefix = "Bearer ";
    if (authHeader.size() > prefix.size() &&
        authHeader.compare(0, prefix.size(), prefix) == 0) {
        std::string token = authHeader.substr(prefix.size());
        auto payload = jwt::verifyToken(token);
        if (payload && payload->isMember("jti") && !(*payload)["jti"].isNull()) {
            std::string jti = (*payload)["jti"].asString();
            int64_t exp = payload->isMember("exp") ? (*payload)["exp"].asInt64() : now;
            co_await auth::blacklist(jti, exp - now);
        }
    }

    // 可选：请求体带 refresh_token 时一并拉黑（防被用来换新 token）。
    auto j = req->getJsonObject();
    if (j) {
        std::string refresh = (*j).get("refresh_token", "").asString();
        if (!refresh.empty()) {
            auto rp = jwt::verifyToken(refresh);
            if (rp && rp->isMember("jti") && !(*rp)["jti"].isNull() &&
                (*rp).get("scope", "") == "refresh") {
                std::string rjti = (*rp)["jti"].asString();
                int64_t rexp = rp->isMember("exp") ? (*rp)["exp"].asInt64() : now;
                co_await auth::blacklist(rjti, rexp - now);
            }
        }
    }

    Json::Value data(Json::objectValue);
    data["logged_out"] = true;
    co_return result::ok(data);
}

Task<HttpResponsePtr> AuthController::refresh(HttpRequestPtr req) {
    auto j = req->getJsonObject();
    if (!j) throw BizError(err::UNAUTHORIZED, "invalid refresh token");
    std::string token = (*j).get("refresh_token", "").asString();
    auto payload = jwt::verifyToken(token);
    if (!payload || (*payload).get("scope", "") != "refresh")
        throw BizError(err::UNAUTHORIZED, "invalid refresh token");
    int64_t uid = 0;
    try {
        uid = std::stoll((*payload)["sub"].asString());
    } catch (...) {
        throw BizError(err::UNAUTHORIZED, "invalid refresh token");
    }
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT id, username, array_to_json(roles) AS roles, enabled FROM sys_users WHERE id = $1",
        uid);
    if (rows.empty() || !rows[0]["enabled"].as<bool>())
        throw BizError(1002, "user disabled or not found");
    const auto &row = rows[0];
    Json::Value extra(Json::objectValue);
    extra["username"] = row["username"].as<std::string>();
    extra["roles"] = util::rowToJson(row)["roles"];
    std::string newToken = jwt::createToken(std::to_string(row["id"].as<int64_t>()), extra);
    Json::Value data(Json::objectValue);
    data["token"] = newToken;
    co_return result::ok(data);
}

Task<HttpResponsePtr> AuthController::verify(HttpRequestPtr req) {
    auth::requireAuth(req);
    auto u = auth::currentUser(req);
    if (!u) throw BizError(err::UNAUTHORIZED, "unauthorized");
    Json::Value data(Json::objectValue);
    data["valid"] = true;
    int64_t id = 0;
    try { id = std::stoll(u->sub); } catch (...) {}
    data["id"] = static_cast<Json::Int64>(id);
    data["username"] = u->username;
    Json::Value arr(Json::arrayValue);
    for (const auto &r : u->roles) arr.append(r);
    data["roles"] = arr;
    co_return result::ok(data);
}

Task<HttpResponsePtr> AuthController::me(HttpRequestPtr req) {
    auth::requireAuth(req);
    auto u = auth::currentUser(req);
    if (!u) throw BizError(err::UNAUTHORIZED, "unauthorized");
    int64_t uid = 0;
    try { uid = std::stoll(u->sub); } catch (...) {}
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT id, username, display_name, array_to_json(roles) AS roles FROM sys_users WHERE id = $1",
        uid);
    if (rows.empty()) throw BizError(err::NOT_FOUND, "user not found");
    const auto &row = rows[0];
    Json::Value data(Json::objectValue);
    data["uid"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
    data["username"] = row["username"].as<std::string>();
    auto dn = row["display_name"];
    data["display_name"] =
        dn.isNull() || dn.as<std::string>().empty() ? row["username"].as<std::string>()
                                                    : dn.as<std::string>();
    data["roles"] = util::rowToJson(row)["roles"];
    co_return result::ok(data);
}

Task<HttpResponsePtr> AuthController::listUsers(HttpRequestPtr req) {
    auth::requireAuth(req);
    int page = std::max(1, ctrl::getIntParam(req, "page", 1));
    int size = std::clamp(ctrl::getIntParam(req, "size", 20), 1, 200);
    std::string keyword = ctrl::getStrParam(req, "keyword");

    std::string where;
    std::string kwArg = keyword.empty() ? std::string() : ("%" + keyword + "%");
    if (!keyword.empty()) {
        where = "WHERE username ILIKE " + ctrl::quoteSql(kwArg) + " OR display_name ILIKE " +
                ctrl::quoteSql(kwArg);
    }
    const std::string cols =
        "id, username, display_name, array_to_json(roles) AS roles, enabled, protected, "
        "created_at, updated_at";
    int64_t total = 0;
    {
        std::string cntSql = "SELECT COUNT(*)::bigint AS c FROM sys_users " + where;
        auto cnt = co_await db::ro()->execSqlCoro(cntSql);
        if (!cnt.empty()) total = cnt[0]["c"].as<int64_t>();
    }
    std::string sql = "SELECT " + cols + " FROM sys_users " + where + " ORDER BY id LIMIT " +
                      std::to_string(size) + " OFFSET " + std::to_string((page - 1) * size);
    auto rows = co_await db::ro()->execSqlCoro(sql);
    co_return result::paginated(total, util::resultToJson(rows));
}

Task<HttpResponsePtr> AuthController::getUser(HttpRequestPtr req, int64_t uid) {
    auth::requireAuth(req);
    auto rows = co_await db::ro()->execSqlCoro(
        "SELECT id, username, display_name, array_to_json(roles) AS roles, enabled, protected, "
        "created_at, updated_at FROM sys_users WHERE id = $1",
        uid);
    if (rows.empty()) throw BizError(err::NOT_FOUND, "user not found");
    co_return result::ok(util::resultToJson(rows)[0]);
}

Task<HttpResponsePtr> AuthController::createUser(HttpRequestPtr req) {
    requireAdmin(req);
    auto j = req->getJsonObject();
    if (!j) throw BizError(err::VALIDATION_FAILED, "request body required");
    const auto &body = *j;
    std::string username = body.get("username", "").asString();
    if (username.empty()) throw BizError(err::VALIDATION_FAILED, "username required");
    auto exist = co_await db::rw()->execSqlCoro("SELECT 1 FROM sys_users WHERE username = $1",
                                                username);
    if (!exist.empty()) throw BizError(err::CONFLICT, "username already exists");
    std::string displayName = body.get("display_name", "").asString();
    if (displayName.empty()) displayName = username;
    std::string password = body.get("password", "").asString();
    if (password.size() < 6) throw BizError(err::VALIDATION_FAILED, "password too short");
    auto roles = jsonStrVec(body["roles"]);
    bool enabled = body.get("enabled", true).asBool();
    bool protectedFlag = body.get("protected", false).asBool();
    int64_t now = nowEpoch();
    auto rows = co_await db::rw()->execSqlCoro(
        "INSERT INTO sys_users (username, display_name, password_hash, roles, enabled, protected, "
        "created_at, updated_at) VALUES ($1, $2, $3, $4::text[], $5, $6, $7, $7) "
        "RETURNING id, username, display_name, array_to_json(roles) AS roles, enabled, protected",
        username, displayName, password::hash(password), pgArrayLiteral(roles), enabled,
        protectedFlag, now);
    co_return result::ok(util::resultToJson(rows)[0]);
}

Task<HttpResponsePtr> AuthController::updateUser(HttpRequestPtr req, int64_t uid) {
    requireAdmin(req);
    auto cur = co_await db::rw()->execSqlCoro("SELECT 1 FROM sys_users WHERE id = $1", uid);
    if (cur.empty()) throw BizError(err::NOT_FOUND, "user not found");
    auto j = req->getJsonObject();
    if (!j) throw BizError(err::VALIDATION_FAILED, "request body required");
    const auto &body = *j;

    std::vector<std::string> sets;
    auto add = [&](const std::string &col, const std::string &val) {
        sets.push_back(col + " = " + ctrl::quoteSql(val));
    };
    if (body.isMember("display_name")) add("display_name", body["display_name"].asString());
    if (body.isMember("roles"))
        add("roles", pgArrayLiteral(jsonStrVec(body["roles"])) + "::text[]");
    if (body.isMember("enabled"))
        add("enabled", std::string(body["enabled"].asBool() ? "true" : "false") + "::boolean");
    if (body.isMember("protected"))
        add("protected",
            std::string(body["protected"].asBool() ? "true" : "false") + "::boolean");
    sets.push_back("updated_at = " + ctrl::quoteSql(std::to_string(nowEpoch())));
    std::string sql = "UPDATE sys_users SET " + ctrl::join(sets, ", ") + " WHERE id = " +
                      ctrl::quoteSql(std::to_string(uid)) +
                      " RETURNING id, username, display_name, array_to_json(roles) AS roles, "
                      "enabled, protected";
    auto rows = co_await db::rw()->execSqlCoro(sql);
    co_return result::ok(util::resultToJson(rows)[0]);
}

Task<HttpResponsePtr> AuthController::deleteUser(HttpRequestPtr req, int64_t uid) {
    requireAdmin(req);
    auto rows = co_await db::rw()->execSqlCoro("SELECT protected FROM sys_users WHERE id = $1",
                                               uid);
    if (rows.empty()) throw BizError(err::NOT_FOUND, "user not found");
    if (rows[0]["protected"].as<bool>())
        throw BizError(err::FORBIDDEN, "protected user cannot be deleted");
    co_await db::rw()->execSqlCoro("DELETE FROM sys_users WHERE id = $1", uid);
    Json::Value d(Json::objectValue);
    d["ok"] = true;
    co_return result::ok(d);
}

Task<HttpResponsePtr> AuthController::changePassword(HttpRequestPtr req, int64_t uid) {
    auth::requireAuth(req);
    auto rows = co_await db::rw()->execSqlCoro(
        "SELECT password_hash FROM sys_users WHERE id = $1", uid);
    if (rows.empty()) throw BizError(err::NOT_FOUND, "user not found");
    auto j = req->getJsonObject();
    if (!j) throw BizError(err::VALIDATION_FAILED, "request body required");
    const auto &body = *j;
    auto u = auth::currentUser(req);
    int64_t caller = 0;
    if (u) try { caller = std::stoll(u->sub); } catch (...) {}
    if (caller != uid && !auth::hasRole(req, "admin"))
        throw BizError(err::FORBIDDEN, "cannot change other user's password");
    if (caller == uid &&
        !password::verify(body.get("old_password", "").asString(),
                          rows[0]["password_hash"].as<std::string>()))
        throw BizError(1001, "old password mismatch");
    std::string newPwd = body.get("new_password", "").asString();
    if (newPwd.size() < 6) throw BizError(err::VALIDATION_FAILED, "new password too short");
    co_await db::rw()->execSqlCoro(
        "UPDATE sys_users SET password_hash = $1, updated_at = $2 WHERE id = $3",
        password::hash(newPwd), nowEpoch(), uid);
    Json::Value d(Json::objectValue);
    d["ok"] = true;
    co_return result::ok(d);
}

Task<HttpResponsePtr> AuthController::setUserEnabled(HttpRequestPtr req, int64_t uid) {
    requireAdmin(req);
    auto j = req->getJsonObject();
    bool enabled = j ? (*j).get("enabled", true).asBool() : true;
    auto rows = co_await db::rw()->execSqlCoro(
        "UPDATE sys_users SET enabled = $1, updated_at = $2 WHERE id = $3 "
        "RETURNING id, username, enabled",
        enabled, nowEpoch(), uid);
    if (rows.empty()) throw BizError(err::NOT_FOUND, "user not found");
    co_return result::ok(util::resultToJson(rows)[0]);
}

Task<HttpResponsePtr> AuthController::setUserRoles(HttpRequestPtr req, int64_t uid) {
    requireAdmin(req);
    auto j = req->getJsonObject();
    if (!j) throw BizError(err::VALIDATION_FAILED, "request body required");
    auto roles = jsonStrVec((*j)["roles"]);
    for (const auto &rc : roles) {
        auto r = co_await db::rw()->execSqlCoro(
            "SELECT 1 FROM sys_roles WHERE role_code = $1", rc);
        if (r.empty()) throw BizError(1003, "role not found: " + rc);
    }
    auto rows = co_await db::rw()->execSqlCoro(
        "UPDATE sys_users SET roles = $1::text[], updated_at = $2 WHERE id = $3 "
        "RETURNING id, username, array_to_json(roles) AS roles",
        pgArrayLiteral(roles), nowEpoch(), uid);
    if (rows.empty()) throw BizError(err::NOT_FOUND, "user not found");
    co_return result::ok(util::resultToJson(rows)[0]);
}

Task<HttpResponsePtr> AuthController::listRoles(HttpRequestPtr req) {
    auth::requireAuth(req);
    auto rows = co_await db::ro()->execSqlCoro("SELECT * FROM sys_roles ORDER BY id");
    auto arr = util::resultToJson(rows);
    co_return result::paginated(static_cast<int64_t>(arr.size()), arr);
}

Task<HttpResponsePtr> AuthController::createRole(HttpRequestPtr req) {
    requireAdmin(req);
    auto j = req->getJsonObject();
    if (!j) throw BizError(err::VALIDATION_FAILED, "request body required");
    const auto &body = *j;
    std::string code = body.get("role_code", "").asString();
    std::string name = body.get("role_name", "").asString();
    if (code.empty() || name.empty())
        throw BizError(err::VALIDATION_FAILED, "role_code and role_name required");
    auto exist = co_await db::rw()->execSqlCoro("SELECT 1 FROM sys_roles WHERE role_code = $1",
                                                code);
    if (!exist.empty()) throw BizError(err::CONFLICT, "role_code already exists");
    Json::Value desc = body["description"];
    int64_t now = nowEpoch();
    auto rows = co_await db::rw()->execSqlCoro(
        "INSERT INTO sys_roles (role_code, role_name, description, created_at, updated_at) "
        "VALUES ($1, $2, $3, $4, $4) RETURNING *",
        code, name, desc.isNull() ? "" : desc.asString(), now);
    co_return result::ok(util::resultToJson(rows)[0]);
}

Task<HttpResponsePtr> AuthController::updateRole(HttpRequestPtr req, int64_t roleId) {
    requireAdmin(req);
    auto cur = co_await db::rw()->execSqlCoro("SELECT 1 FROM sys_roles WHERE id = $1", roleId);
    if (cur.empty()) throw BizError(err::NOT_FOUND, "role not found");
    auto j = req->getJsonObject();
    if (!j) throw BizError(err::VALIDATION_FAILED, "request body required");
    const auto &body = *j;
    std::vector<std::string> sets;
    if (body.isMember("role_name")) {
        sets.push_back("role_name = " + ctrl::quoteSql(body["role_name"].asString()));
    }
    if (body.isMember("description")) {
        sets.push_back("description = " +
                       ctrl::quoteSql(body["description"].isNull()
                                          ? std::string()
                                          : body["description"].asString()));
    }
    sets.push_back("updated_at = " + ctrl::quoteSql(std::to_string(nowEpoch())));
    std::string sql = "UPDATE sys_roles SET " + ctrl::join(sets, ", ") + " WHERE id = " +
                      ctrl::quoteSql(std::to_string(roleId)) + " RETURNING *";
    auto rows = co_await db::rw()->execSqlCoro(sql);
    co_return result::ok(util::resultToJson(rows)[0]);
}

Task<HttpResponsePtr> AuthController::deleteRole(HttpRequestPtr req, int64_t roleId) {
    requireAdmin(req);
    auto rows = co_await db::rw()->execSqlCoro(
        "SELECT role_code FROM sys_roles WHERE id = $1", roleId);
    if (rows.empty()) throw BizError(err::NOT_FOUND, "role not found");
    if (rows[0]["role_code"].as<std::string>() == "admin")
        throw BizError(err::FORBIDDEN, "admin role cannot be deleted");
    co_await db::rw()->execSqlCoro("DELETE FROM sys_roles WHERE id = $1", roleId);
    Json::Value d(Json::objectValue);
    d["ok"] = true;
    co_return result::ok(d);
}

}  // namespace ibmcp