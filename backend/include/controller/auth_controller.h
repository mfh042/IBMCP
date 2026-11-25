#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class AuthController : public drogon::HttpController<AuthController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::login, "/api/v1/auth/login", drogon::Post);
    ADD_METHOD_TO(AuthController::logout, "/api/v1/auth/logout", drogon::Post);
    ADD_METHOD_TO(AuthController::refresh, "/api/v1/auth/refresh", drogon::Post);
    ADD_METHOD_TO(AuthController::verify, "/api/v1/auth/verify", drogon::Get);
    ADD_METHOD_TO(AuthController::me, "/api/v1/auth/me", drogon::Get);
    ADD_METHOD_TO(AuthController::listUsers, "/api/v1/auth/users", drogon::Get);
    ADD_METHOD_TO(AuthController::getUser, "/api/v1/auth/users/{1}", drogon::Get);
    ADD_METHOD_TO(AuthController::createUser, "/api/v1/auth/users", drogon::Post);
    ADD_METHOD_TO(AuthController::updateUser, "/api/v1/auth/users/{1}", drogon::Put);
    ADD_METHOD_TO(AuthController::deleteUser, "/api/v1/auth/users/{1}", drogon::Delete);
    ADD_METHOD_TO(AuthController::changePassword, "/api/v1/auth/users/{1}/password",
                  drogon::Put);
    ADD_METHOD_TO(AuthController::setUserEnabled, "/api/v1/auth/users/{1}/enabled",
                  drogon::Put);
    ADD_METHOD_TO(AuthController::setUserRoles, "/api/v1/auth/users/{1}/roles", drogon::Put);
    ADD_METHOD_TO(AuthController::listRoles, "/api/v1/auth/roles", drogon::Get);
    ADD_METHOD_TO(AuthController::createRole, "/api/v1/auth/roles", drogon::Post);
    ADD_METHOD_TO(AuthController::updateRole, "/api/v1/auth/roles/{1}", drogon::Put);
    ADD_METHOD_TO(AuthController::deleteRole, "/api/v1/auth/roles/{1}", drogon::Delete);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> login(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> logout(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> refresh(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> verify(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> me(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> listUsers(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> getUser(drogon::HttpRequestPtr req, int64_t uid);
    drogon::Task<drogon::HttpResponsePtr> createUser(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> updateUser(drogon::HttpRequestPtr req,
                                                     int64_t uid);
    drogon::Task<drogon::HttpResponsePtr> deleteUser(drogon::HttpRequestPtr req,
                                                     int64_t uid);
    drogon::Task<drogon::HttpResponsePtr> changePassword(drogon::HttpRequestPtr req,
                                                         int64_t uid);
    drogon::Task<drogon::HttpResponsePtr> setUserEnabled(drogon::HttpRequestPtr req,
                                                         int64_t uid);
    drogon::Task<drogon::HttpResponsePtr> setUserRoles(drogon::HttpRequestPtr req,
                                                       int64_t uid);
    drogon::Task<drogon::HttpResponsePtr> listRoles(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> createRole(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> updateRole(drogon::HttpRequestPtr req,
                                                     int64_t roleId);
    drogon::Task<drogon::HttpResponsePtr> deleteRole(drogon::HttpRequestPtr req,
                                                     int64_t roleId);
};

}  // namespace ibmcp