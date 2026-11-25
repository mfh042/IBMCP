# 03 - 接口契约与 Controller 映射

> 对外接口保持完全兼容：前缀仍为 `/api/v1`，统一响应包膜 `{code, message, data}`（HTTP 恒 200，`code=0` 成功），WebSocket 路径 `/ws/push`（兼容旧 `/api/v1/ws/collect`）。前端无需改动。

## 1. Controller 拆分方案

将旧 9 个路由模块 + ping 映射为 13 个 Drogon `HttpController` 子类 + 1 个 `WebSocketController`：

| 新 Controller | 路径前缀 | 对应旧文件 |
|---------------|----------|-----------|
| `SystemController` | `/api/v1/ping` | `router.py` |
| `AuthController` | `/api/v1/auth` | `auth.py` |
| `StaticDictController` | `/api/v1/static` | `static_dict.py` |
| `ConfigController` | `/api/v1/config/equipments`, `/api/v1/config/revisions` | `running_config.py`（equipments/revisions 部分） |
| `ConfigRouteController` | `/api/v1/config/routes` | `running_config.py`（routes 部分） |
| `ConfigDeviceController` | `/api/v1/config/devices` | `running_config.py`（devices 部分） |
| `ConfigPointController` | `/api/v1/config/points` | `running_config.py`（points 部分） |
| `ConfigFunctionController` | `/api/v1/config/functions` | `running_config.py`（functions 部分） |
| `DataController` | `/api/v1/data` | `data.py` |
| `RuntimeDeviceController` | `/api/v1/runtime/devices` | `devices.py` |
| `AlarmController` | `/api/v1/alarms` | `alarms.py` |
| `ControlController` | `/api/v1/control` | `control.py` |
| `CollectorAdminController` | `/api/v1/collector` | `collector_admin.py` |
| `WsPushController`（WS） | `/ws/push`, `/api/v1/ws/collect` | `ws.py` |

> `running_config.py` 单文件 32 个端点，按资源拆为 5 个 Controller，避免单文件过大，与「五级配置链」的资源边界对齐。

## 2. Drogon Controller 写法（C++20 协程）

每个 Controller 继承 `drogon::HttpController<Derived>`，用宏注册方法。示例（`SystemController`）：

```cpp
// include/ibmcp/controller/system_controller.h
#pragma once
#include <drogon/HttpController.h>
namespace ibmcp {

class SystemController : public drogon::HttpController<SystemController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SystemController::ping, "/api/v1/ping", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> ping(const drogon::HttpRequestPtr& req);
};

} // namespace ibmcp
```

```cpp
// src/controller/system_controller.cpp
#include "ibmcp/controller/system_controller.h"
#include "ibmcp/core/result.h"
#include <chrono>

using namespace ibmcp;
using namespace drogon;

Task<HttpResponsePtr> SystemController::ping(const HttpRequestPtr&) {
    // Result::ok(data) 生成 {code:0, message:"ok", data:{...}} 的统一包膜
    co_return Result::ok(nlohmann::json{
        {"status", "ok"},
        {"time", std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch()).count()}
    });
}
```

带路径参数的示例（`RuntimeDeviceController`）：鉴权由 `main.cpp` 的全局 Post-Routing Advice 统一处理，路由声明无需带 Filter 参数。

```cpp
ADD_METHOD_TO(RuntimeDeviceController::getDevice,
              "/api/v1/runtime/devices/{device_code}", drogon::Get);
ADD_METHOD_TO(RuntimeDeviceController::control,
              "/api/v1/runtime/devices/{device_code}/control", drogon::Post);

Task<HttpResponsePtr> RuntimeDeviceController::control(
        const HttpRequestPtr& req, std::string&& deviceCode) {
    auto body = req->getJsonObject();
    if (!body) co_return Result::fail(400, "请求体必须为 JSON");
    auto pointId = (*body).value("point_id", 0);
    auto value   = (*body).value("value", "");
    auto wait    = (*body).value("wait", false);
    // ... 调用 ControlService，co_await 结果
}
```

## 3. 完整端点映射表

下表覆盖全部 78 个端点。`Auth` 列：`✓` 表示需登录（由全局鉴权 Advice 校验），`—` 表示公开（advice 白名单放行）。

### 3.1 system / ping

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| GET | `/api/v1/ping` | — | `router.py:20` | `SystemController::ping` |

### 3.2 auth

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| POST | `/api/v1/auth/login` | — | `auth.py:69` | `AuthController::login` |
| POST | `/api/v1/auth/refresh` | — | `auth.py:94` | `AuthController::refresh` |
| POST | `/api/v1/auth/logout` | ✓ | — | `AuthController::logout`（jti 写入 Redis 黑名单，登出即失效，详见 09 §5） |
| GET | `/api/v1/auth/verify` | ✓ | `auth.py:113` | `AuthController::verify` |
| GET | `/api/v1/auth/me` | ✓ | `auth.py:123` | `AuthController::me` |
| GET | `/api/v1/auth/users` | ✓ | `auth.py:138` | `AuthController::listUsers` |
| GET | `/api/v1/auth/users/{uid}` | ✓ | `auth.py:164` | `AuthController::getUser` |
| POST | `/api/v1/auth/users` | ✓ | `auth.py:176` | `AuthController::createUser` |
| PUT | `/api/v1/auth/users/{uid}` | ✓ | `auth.py:199` | `AuthController::updateUser` |
| DELETE | `/api/v1/auth/users/{uid}` | ✓ | `auth.py:228` | `AuthController::deleteUser` |
| PUT | `/api/v1/auth/users/{uid}/password` | ✓ | `auth.py:241` | `AuthController::changePassword` |
| PUT | `/api/v1/auth/users/{uid}/enabled` | ✓ | `auth.py:266` | `AuthController::setUserEnabled` |
| PUT | `/api/v1/auth/users/{uid}/roles` | ✓ | `auth.py:283` | `AuthController::setUserRoles` |
| GET | `/api/v1/auth/roles` | ✓ | `auth.py:303` | `AuthController::listRoles` |
| POST | `/api/v1/auth/roles` | ✓ | `auth.py:309` | `AuthController::createRole` |
| PUT | `/api/v1/auth/roles/{role_id}` | ✓ | `auth.py:326` | `AuthController::updateRole` |
| DELETE | `/api/v1/auth/roles/{role_id}` | ✓ | `auth.py:350` | `AuthController::deleteRole` |

### 3.3 static（字典资源通用 CRUD）

`{resource}` 为字典编码（`device_category` / `device_port` / ...）。

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| GET | `/api/v1/static/bootstrap` | ✓ | `static_dict.py:287` | `StaticDictController::bootstrap` |
| GET | `/api/v1/static/{resource}` | ✓ | `static_dict.py:305` | `StaticDictController::page` |
| GET | `/api/v1/static/{resource}/all` | ✓ | `static_dict.py:347` | `StaticDictController::all` |
| GET | `/api/v1/static/{resource}/code/{code}` | ✓ | `static_dict.py:359` | `StaticDictController::byCode` |
| GET | `/api/v1/static/{resource}/{row_id}` | ✓ | `static_dict.py:376` | `StaticDictController::byId` |
| POST | `/api/v1/static/{resource}` | ✓ | `static_dict.py:391` | `StaticDictController::create` |
| PUT | `/api/v1/static/{resource}/{row_id}` | ✓ | `static_dict.py:424` | `StaticDictController::update` |
| DELETE | `/api/v1/static/{resource}/{row_id}` | ✓ | `static_dict.py:461` | `StaticDictController::remove` |

### 3.4 config（五级运行配置）

**equipments / revisions：**

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| GET | `/api/v1/config/equipments` | ✓ | `running_config.py:41` | `ConfigController::listEquipments` |
| GET | `/api/v1/config/equipments/code/{code}` | ✓ | `running_config.py:65` | `ConfigController::equipmentByCode` |
| GET | `/api/v1/config/equipments/{config_id}` | ✓ | `running_config.py:76` | `ConfigController::getEquipment` |
| GET | `/api/v1/config/equipments/{config_id}/status` | ✓ | `running_config.py:85` | `ConfigController::equipmentStatus` |
| GET | `/api/v1/config/equipments/{config_id}/expand` | ✓ | `running_config.py:120` | `ConfigController::equipmentExpand` |
| GET | `/api/v1/config/equipments/{config_id}/plan` | ✓ | `running_config.py:151` | `ConfigController::equipmentPlan` |
| GET | `/api/v1/config/revisions` | ✓ | `running_config.py:162` | `ConfigController::listRevisions` |
| POST | `/api/v1/config/equipments` | ✓ | `running_config.py:177` | `ConfigController::createEquipment` |
| PUT | `/api/v1/config/equipments/{config_id}` | ✓ | `running_config.py:198` | `ConfigController::updateEquipment` |
| POST | `/api/v1/config/equipments/{config_id}/activate` | ✓ | `running_config.py:222` | `ConfigController::activateEquipment` |
| POST | `/api/v1/config/equipments/{config_id}/unload` | ✓ | `running_config.py:242` | `ConfigController::unloadEquipment` |
| POST | `/api/v1/config/equipments/recompile` | ✓ | `running_config.py:258` | `ConfigController::recompileAll` |
| DELETE | `/api/v1/config/equipments/{config_id}` | ✓ | `running_config.py:270` | `ConfigController::deleteEquipment` |
| POST | `/api/v1/config/equipments/{config_id}/sync` | ✓ | `running_config.py:295` | `ConfigController::syncEquipment` |
| DELETE | `/api/v1/config/equipments/{config_id}/cleanup` | ✓ | `running_config.py:312` | `ConfigController::cleanupEquipment` |

**routes：**

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| GET | `/api/v1/config/routes` | ✓ | `running_config.py:324` | `ConfigRouteController::list` |
| POST | `/api/v1/config/routes` | ✓ | `running_config.py:341` | `ConfigRouteController::create` |
| GET | `/api/v1/config/routes/{route_id}/expand` | ✓ | `running_config.py:354` | `ConfigRouteController::expand` |
| PUT | `/api/v1/config/routes/{route_id}` | ✓ | `running_config.py:374` | `ConfigRouteController::update` |
| DELETE | `/api/v1/config/routes/{route_id}` | ✓ | `running_config.py:391` | `ConfigRouteController::remove` |

**devices（路线步骤绑定设备）：**

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| GET | `/api/v1/config/devices` | ✓ | `running_config.py:404` | `ConfigDeviceController::list` |
| POST | `/api/v1/config/devices` | ✓ | `running_config.py:421` | `ConfigDeviceController::create` |
| PUT | `/api/v1/config/devices/{binding_id}` | ✓ | `running_config.py:434` | `ConfigDeviceController::update` |
| DELETE | `/api/v1/config/devices/{binding_id}` | ✓ | `running_config.py:451` | `ConfigDeviceController::remove` |

**points：**

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| GET | `/api/v1/config/points` | ✓ | `running_config.py:461` | `ConfigPointController::list` |
| POST | `/api/v1/config/points` | ✓ | `running_config.py:478` | `ConfigPointController::create` |
| PUT | `/api/v1/config/points/{point_id}` | ✓ | `running_config.py:492` | `ConfigPointController::update` |
| DELETE | `/api/v1/config/points/{point_id}` | ✓ | `running_config.py:509` | `ConfigPointController::remove` |

**functions：**

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| GET | `/api/v1/config/functions` | ✓ | `running_config.py:518` | `ConfigFunctionController::list` |
| POST | `/api/v1/config/functions` | ✓ | `running_config.py:535` | `ConfigFunctionController::create` |
| PUT | `/api/v1/config/functions/{func_id}` | ✓ | `running_config.py:549` | `ConfigFunctionController::update` |
| DELETE | `/api/v1/config/functions/{func_id}` | ✓ | `running_config.py:566` | `ConfigFunctionController::remove` |

### 3.5 data（时序数据查询）

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| GET | `/api/v1/data/history` | ✓ | `data.py:20` | `DataController::history` |
| GET | `/api/v1/data/latest` | ✓ | `data.py:37` | `DataController::latest` |

### 3.6 runtime（运行时设备视图）

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| GET | `/api/v1/runtime/devices` | ✓ | `devices.py:16` | `RuntimeDeviceController::list` |
| GET | `/api/v1/runtime/devices/overview` | ✓ | `devices.py:38` | `RuntimeDeviceController::overview` |
| GET | `/api/v1/runtime/devices/{device_code}` | ✓ | `devices.py:51` | `RuntimeDeviceController::get` |
| GET | `/api/v1/runtime/devices/{device_code}/points` | ✓ | `devices.py:60` | `RuntimeDeviceController::points` |
| GET | `/api/v1/runtime/devices/{device_code}/points/{point_id}` | ✓ | `devices.py:69` | `RuntimeDeviceController::point` |
| POST | `/api/v1/runtime/devices/{device_code}/control` | ✓ | `devices.py:80` | `RuntimeDeviceController::control` |
| GET | `/api/v1/runtime/devices/{device_code}/last-values` | ✓ | `devices.py:99` | `RuntimeDeviceController::lastValues` |

### 3.7 alarms

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| GET | `/api/v1/alarms/list` | ✓ | `alarms.py:15` | `AlarmController::list` |
| GET | `/api/v1/alarms/stats` | ✓ | `alarms.py:42` | `AlarmController::stats` |
| GET | `/api/v1/alarms/{alarm_id}` | ✓ | `alarms.py:54` | `AlarmController::get` |
| PUT | `/api/v1/alarms/{alarm_id}/ack` | ✓ | `alarms.py:63` | `AlarmController::ack` |
| PUT | `/api/v1/alarms/{alarm_id}/recover` | ✓ | `alarms.py:81` | `AlarmController::recover` |

### 3.8 control

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| POST | `/api/v1/control/write` | ✓ | `control.py:15` | `ControlController::write` |
| GET | `/api/v1/control/commands/{command_id}` | ✓ | `control.py:47` | `ControlController::command` |
| GET | `/api/v1/control/queue-depth` | ✓ | `control.py:58` | `ControlController::queueDepth` |

### 3.9 collector（采集器管理）

| 方法 | 路径 | Auth | 旧位置 | 新方法 |
|------|------|------|--------|--------|
| GET | `/api/v1/collector/health` | ✓ | `collector_admin.py:12` | `CollectorAdminController::health` |
| GET | `/api/v1/collector/protocols` | ✓ | `collector_admin.py:20` | `CollectorAdminController::protocols` |
| GET | `/api/v1/collector/stats` | ✓ | `collector_admin.py:28` | `CollectorAdminController::stats` |
| POST | `/api/v1/collector/reload` | ✓ | `collector_admin.py:43` | `CollectorAdminController::reload` |

### 3.10 websocket

| 方法 | 路径 | Auth | 旧位置 | 新类 |
|------|------|------|--------|------|
| WS | `/ws/push` | ✓（首帧 token） | `ws.py:8`（顶层路由 `main.py:73`） | `WsPushController` |
| WS | `/api/v1/ws/collect` | ✓（兼容旧路径） | `ws.py:8` | `WsPushController` |

## 4. 统一响应包膜

### 4.1 响应格式

所有 HTTP 响应体统一为：

```json
{ "code": 0, "message": "ok", "data": { } }
```

- `code=0` 成功；非 0 为业务错误码（HTTP 状态仍为 200，与旧 `ResultEnvelopeMiddleware` 一致）；
- `data` 为业务数据（对象/数组/null）。

### 4.2 实现：`Result` 工具类 + 全局异常

```cpp
// include/ibmcp/core/result.h
#pragma once
#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>

namespace ibmcp {
class Result {
public:
    static drogon::HttpResponsePtr ok(const nlohmann::json& data = nullptr) {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(
            nlohmann::json{{"code", 0}, {"message", "ok"}, {"data", data}});
        resp->setStatusCode(drogon::k200OK);
        return resp;
    }
    static drogon::HttpResponsePtr fail(int code, const std::string& message,
                                        const nlohmann::json& data = nullptr) {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(
            nlohmann::json{{"code", code}, {"message", message}, {"data", data}});
        resp->setStatusCode(drogon::k200OK);  // 业务错误 HTTP 仍 200
        return resp;
    }
};
} // namespace ibmcp
```

业务异常：定义 `BizException(int code, std::string msg)`，Controller 抛出后由 Drogon 全局异常 handler（`app().setExceptionHandler`）捕获并转 `Result::fail`。

> Drogon 的 `setExceptionHandler` 可统一捕获未处理异常；自定义 `BizException` 走业务错误码，其它未知异常走 500 + code=-1。

## 5. JWT 鉴权（全局 Post-Routing Advice）

旧 `Depends(get_current_user)` → Drogon 全局 **后置路由 Advice**：在 `backend/src/main.cpp` 中调用 `app().registerPostRoutingAdvice`（3 参数拦截器：`req, AdviceCallback, AdviceChainCallback`）注册，运行于「路由匹配之后、Handler 执行之前」，对所有请求统一拦截。

> **为什么不是「每个路由挂 `AuthFilter`」**：Drogon 1.9.5 中 `HttpFilter<T>` 子类由框架自动实例化，**不能**通过 `registerFilter` / `registerPostRoutingFilter` 手动挂载到路由（编译期 `static_assert` 会报错 "Filters created and initialized automatically by drogon cannot be registered here"）。因此采用一条全局 advice 统一鉴权，效果等价。`backend/include/filter/auth_filter.h` 中的 `AuthFilter` 类保留作参考实现，但当前未被任何路由引用。

逻辑（移植自旧 `backend/app/core/security.py`）：
1. 取 `Authorization: Bearer <token>` 头；缺失 → `adviceCb(error(401, "missing bearer token"))` 短路；
2. `ibmcp::jwt::verifyToken(token)` 校验签名（密钥 `APP_SECRET_KEY`，HS256）与过期；失败 → 401；
3. 取载荷 `jti`：为空则放行（兼容存量旧 token 至自然过期）；非空则 `auth::isBlacklisted(jti, cb)` **异步**查 Redis 黑名单——命中 → `adviceCb(error(401, "token revoked"))` 短路，未命中 → 继续；Redis 异常 / 超时走 fail-open 放行（详见 09 §5）；
4. 通过则把 JWT 载荷写入 `req->attributes()` 的 `"user"` 属性（`Json::Value`），调用 `chainCb()` 放行进入 Handler。

> 注意：本 API 所有业务响应 **HTTP 恒 200**，状态在 body `code`（`result::make` 恒设 `k200OK`）。故「token 已失效 / 缺失 / 非法」均为 HTTP 200 + body `code=401`（如 `{"code":401,"message":"token revoked"}`），前端与测试须判 body `code` 而非 HTTP 状态。

```cpp
// main.cpp 中的关键写入
req->getAttributes()->insert("user", *payload);   // payload 为 Json::Value
chainCb();
```

Controller 中取当前用户（推荐用 `core/auth.h` 的辅助函数，避免直接读属性）：

```cpp
// include/core/auth.h
const Json::Value &user = ibmcp::auth::requireAuth(req);   // 未登录抛 BizError(401)
int64_t userId = user["id"].as<int64_t>();
```

### 5.1 公开白名单

以下路径在 advice 中直接放行（不校验 token）：
- `ibmcp::config::isPublicPath(path)` 返回真的路径：白名单实际来自 `config.json` 的 `custom_config.public_paths` 数组（`backend/config.json:55`，`core/config.cpp:70` 读取），当前为 `/api/v1/auth/login`、`/api/v1/auth/refresh`、`/api/v1/ping`、`/api/v1/openapi.json`、`/api/v1/docs`、`/api/v1/redoc`；
- WebSocket 端点 `/ws/push`、`/api/v1/ws/collect`（首帧 query 自带 token，在对应 `WebSocketController::handleNewConnection` 中校验）。

其余全部需带有效 Bearer token。

## 6. 参数校验

旧 Pydantic 的请求体验证 → C++ 用 nlohmann/json 显式校验。建议：
- 每个写接口定义 DTO struct + `fromJson(const nlohmann::json&)` 静态方法，集中校验必填字段与类型，失败抛 `BizException(400, "缺少字段 xxx")`；
- 分页参数（`page`/`page_size`/`keyword` 等）从 `req->getParameter()` 取，给默认值并 clamp 范围；
- 路径参数由 Drogon 宏自动解析绑定到方法形参（`{config_id}` → `int64_t&&`）。

## 7. WebSocket 控制器

`WsPushController` 继承 `drogon::WebSocketController<WsPushController>`，注册两个路径：

```cpp
WS_PATH_LIST_BEGIN
WS_PATH_ADD("/ws/push");
WS_PATH_ADD("/api/v1/ws/collect");  // 兼容旧路径
WS_PATH_LIST_END
```

关键方法：
- `handleNewConnection(req, ctx)`：首帧从 query 或首条消息取 token，校验通过则加入 `WsHub`，否则关闭连接（401）；
- `handleNewMessage(ctx, msg, type)`：按消息 `topic` 字段分发（订阅/取消订阅四主题）；
- `handleConnectionClosed(ctx)`：从 `WsHub` 移除连接。

四主题（与旧 `ws/topics.py` 一致）：`data`（实时采样）、`device`（设备状态）、`alarm`（报警）、`control`（控制命令状态）。`WsHub` 维护「连接 → 已订阅主题集合」与「主题 → 连接集合」，广播时只推给订阅者。

## 8. OpenAPI 文档

Drogon 不自动生成 OpenAPI。两种选择：
1. **不生成**：前端已固定对接，接口稳定，可省略（推荐，减少维护）；
2. **静态托管**：把现有 FastAPI 导出的 `openapi.json` 拷到 `/api/v1/openapi.json` 作为静态文件，仅作参考，不随代码自动更新。

采用方案 1，删除旧 `openapi_url`/`docs_url`/`redoc_url`。