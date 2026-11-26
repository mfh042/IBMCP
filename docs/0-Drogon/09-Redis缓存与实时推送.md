# 09 · Redis 缓存、实时推送与会话黑名单

> Redis 已从「预留组件」落地为后端核心运行时依赖，承载三项能力：**响应数据缓存**、**基于 Pub/Sub 的 WS 实时推送骨干**、**Token 黑名单（登出即失效）**。
> 本文基于 `backend/` 真实源码编写，键名 / TTL / 调用点均与代码一致。

## 1. 概述

| 能力 | 解决的问题 | 关键机制 |
|------|-----------|---------|
| 响应数据缓存 | 字典 / 配置 / 概览等读多写少接口重复查库 | Cache-Aside：读时回填、写时失效，`cache:getOrSet` |
| 实时推送骨干 | 原进程内广播仅限单实例，无法水平扩展 | Redis Pub/Sub：`broadcast` → `PUBLISH`，本地订阅者经 `psubscribe` 接收 |
| Token 黑名单 | JWT 无状态，登出后旧 token 仍有效至过期 | 每条 token 带唯一 `jti`，登出写入 `auth:blacklist:{jti}` 并设 TTL = 剩余有效期，鉴权时异步查黑名单 |

设计原则：
- **fail-open（失效放行）**：Redis 不可用时不崩、不 500。缓存回源数据库、鉴权放行、推送仅记日志，HTTP 服务始终可用。
- **有界超时**：`config.json` 设 `timeout=1.0`（秒），Redis 宕机时命令 ≤1s 失败触发 fail-open，避免请求无限挂起。
- **写时失效优先于主动过期**：高频读路径（列表 / 概览 / 聚合）写后主动 `DEL`；分页 / 单条 / 按码变体靠 TTL 自然过期以控制失效成本。

## 2. Redis 客户端与配置

配置位于 [backend/config.json](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/config.json) 的 `redis_clients` 段：

```json
"redis_clients": [{
  "name": "default", "host": "127.0.0.1", "port": 6379,
  "passwd": "", "db": 0,
  "is_fast": false, "number_of_connections": 5,
  "timeout": 1.0
}]
```

- `name=default`：代码通过 `drogon::app().getRedisClient("default")` 获取，封装于 [backend/src/db/redis.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/db/redis.cpp) 的 `redis::client()`。
- `timeout=1.0`：**关键参数**。Drogon `RedisClient::setTimeout`（0 / 负值 = 无界）。设为 1s 后，Redis 不可用时缓存读 / 黑名单查询在 ~1s 内以 `Command execution timeout` 失败并走 fail-open，避免请求挂起数十秒。
- 健康探针：`GET /api/v1/redis/ping` 返回 `{"code":0,"data":{"redis":"ok","ping":"PONG","get":"1","dbsize":...}}`（[system_controller.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/system_controller.cpp) 的 `SystemController::redisPing`）。
- 部署：Docker 容器 `ibmcp-redis`（redis7）映射 `6379`，由 [deployments/openeuler/docker-compose.yml](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/deployments/openeuler/docker-compose.yml) 拉起。

## 3. 代码结构

| 文件 | 职责 |
|------|------|
| [include/core/cache.h](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/include/core/cache.h) / [src/core/cache.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/core/cache.cpp) | 缓存公共层：`getOrSet` / `getJson` / `setJson` / `invalidate` / `invalidateMatch` |
| [include/core/auth_redis.h](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/include/core/auth_redis.h) / [src/core/auth_redis.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/core/auth_redis.cpp) | 黑名单：`blacklist(jti, ttl)` / `isBlacklisted(jti, cb)` |
| [include/ws/ws_hub.h](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/include/ws/ws_hub.h) / [src/ws/ws_hub.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/ws/ws_hub.cpp) | WS 枢纽：订阅 / `broadcast` / Redis `psubscribe` + 本地投递 |
| [src/core/jwt_util.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/core/jwt_util.cpp) | 签发 token 时写入唯一 `jti`（128-bit 随机 hex） |
| [src/main.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/main.cpp) | 启动期 `WsHub::init()`；全局 Post-Routing Advice 接入黑名单检查 |
| `src/controller/*` | 各读接口接入 `cache::getOrSet`，各写接口调用 `cache::invalidate` / `invalidateMatch`；[auth_controller.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/auth_controller.cpp) 提供 `logout` |

## 4. 能力一：响应数据缓存

### 4.1 公共接口

定义于 [cache.h](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/include/core/cache.h)：

```cpp
constexpr const char *kPrefix = "cache:";

Task<Json::Value>  getOrSet(std::string_view key, int ttl, Loader loader);  // 命中返回，未命中回源+回填
Task<std::optional<Json::Value>> getJson(std::string_view key);
Task<void> setJson(std::string_view key, const Json::Value &value, int ttl);
Task<void> invalidate(std::string_view key);
Task<void> invalidateMatch(std::string_view pattern);   // SCAN + DEL 批量
```

- 全部为 C++20 协程（`execCommandCoro`），内部 `try/catch` 吞掉 Redis 异常 → 返回 `nullopt` / `false`，**绝不向上抛**，保证 fail-open。
- `getOrSet`：命中即返回；未命中或读取异常 → 执行 `loader` 回源（通常查 PG）→ 尝试回写缓存（失败仅 `LOG_ERROR`）→ 始终返回源数据。

### 4.2 fail-open 语义

Redis 不可用时：`getJson` 捕获异常 → 返回 `nullopt` → 上层走 `loader` 回源 PG → 接口仍返回 **HTTP 200 + 正确数据**（仅首屏略慢）。`setJson` / `invalidate` 失败仅记日志，不影响主流程。

### 4.3 缓存键命名空间与 TTL

| 前缀 / 键模式 | TTL | 写入点（读接口） | 说明 |
|--------------|-----|------------------|------|
| `cache:static_dict:bootstrap` | 600 | `StaticDictController::bootstrap` | 前端启动字典聚合（全部字典 items） |
| `cache:static_dict:{resource}:all` | 600 | `list`（无分页） | 某字典全量 |
| `cache:static_dict:{resource}:page:{p}:{s}:{kw}:{fk}` | 600 | `list`（分页） | 分页结果，含关键词/筛选参数 |
| `cache:static_dict:{resource}:code:{code}` | 600 | `getByCode` | 按业务码查单条 |
| `cache:static_dict:{resource}:{id}` | 600 | `getOne` | 按主键查单条 |
| `cache:config:routes:cfg:{configId}` | 300 | `ConfigRouteController::list` | 某配置下的采集方案列表 |
| `cache:config:routes:{routeId}:expand` | 300 | `ConfigRouteController::expand` | 方案展开（内嵌 steps / device bindings / points） |
| `cache:config:devices:route:{routeId}` | 300 | `ConfigDeviceController::list` | 方案下的设备绑定列表 |
| `cache:config:functions:cfg:{configId}` | 300 | `ConfigFunctionController::list` | 设备功能列表 |
| `cache:config:points:*` | 300 | `ConfigPointController::*` | 采集点列表 / 详情 |
| `cache:runtime:devices:overview` | 30 | `RuntimeDeviceController::overview` | 运行概览（含 `active_alarm_count`） |
| `cache:runtime:devices:p{p}:s{s}:st{status}` | 30 | `RuntimeDeviceController::list` | 运行设备分页列表 |
| `cache:data:latest:{deviceCode}:{points}` | 5 | `DataController::getLatest` | 某设备最新采集值（多测点） |
| — | — | `DataController::getHistory` | **不缓存**（历史时序每次回源） |

> TTL 常量：`static_dict` 用 `kDictTtl=600`，`config` 系用 `kConfigTtl=300`，`runtime/devices` 用 `kRuntimeTtl=30`，`data/latest` 用 `kDataTtl=5`，均定义在各控制器匿名命名空间。

### 4.4 写时失效映射

写接口必须在 DB 写成功后调用失效（注意：本 API 所有响应 HTTP 恒 200，写失败时 body `code` 非 0，此时**不应**失效缓存）：

| 写操作（控制器） | 失效的键模式 | 说明 |
|-----------------|-------------|------|
| `StaticDictController` create/update/remove/drop | `cache:static_dict:bootstrap` + `cache:static_dict:{resource}:all` | 字典写后失效聚合与全量；分页/单条/按码变体靠 TTL 自然过期 |
| `ConfigRouteController` create/update/remove | `cache:config:routes*` | 覆盖列表 + 全部 expand |
| `ConfigDeviceController` create/update/remove | `cache:config:devices*` **+** `cache:config:routes*` | device binding 内嵌于 route expand，必须连带失效 |
| `ConfigPointController` create/update/remove | `cache:config:points*` **+** `cache:config:routes*` | point 内嵌于 route expand，必须连带失效 |
| `ConfigFunctionController` create/update/remove | `cache:config:functions*` | — |
| `ControlController` 控制下发（写 using_command） | `cache:runtime:devices*` | 控制可能改变设备运行态 |
| `AlarmController` ack / recover | `cache:runtime:devices*` | overview 含 `active_alarm_count`，告警状态变更后必须失效 |

> `cache:config:routes*` 的连带失效来自代码审查发现的脏读缺口修复（见 [review.md](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/.trae/specs/redis-advanced-features/review.md) Finding 1）：编辑设备绑定 / 采集点后 `/config/routes/{id}/expand` 不再最长 300s 脏读。

## 5. 能力二：Token 黑名单 / 会话失效

### 5.1 jti（JWT ID）

- [jwt_util.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/core/jwt_util.cpp) `createToken` 每次签发写入 `jti`：128-bit 随机数转 hex（32 字符），跨登录唯一。
- access token 有效期由 `config.json` 的 `app.token_expire_minutes = 1440`（24 小时）控制。

### 5.2 登出端点 `POST /api/v1/auth/logout`

需鉴权（走全局 Advice），实现于 [auth_controller.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/auth_controller.cpp)：

1. 从 `Authorization: Bearer` 取 access token，`verifyToken` 取出 `jti` / `exp`；
2. `co_await auth::blacklist(jti, exp - now)`（剩余秒数 ≤ 0 则不写，Redis 侧自动到期）；
3. 若请求体带 `refresh_token`：`verifyToken` 校验签名+过期，且 **`scope == "refresh"`**（与 refresh 端点一致），取其 `jti`/`exp` 一并拉黑（防止被用来换新 token）；
4. 返回 `result::ok()`（body `code = 0`）。

### 5.3 黑名单检查（全局 Post-Routing Advice）

在 [main.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/main.cpp) 通过 `registerPostRoutingAdvice` 注册，运行于「路由匹配后、Handler 前」：

1. 取 `Authorization: Bearer`，缺失 → `adviceCb(error(401, "missing bearer token"))` 短路；
2. `jwt::verifyToken` 校验签名与过期，失败 → 401；
3. 取载荷 `jti`：
   - **jti 为空** → 放行（兼容存量旧 token 直至自然过期，不强制全部重新登录）；
   - **jti 非空** → `auth::isBlacklisted(jti, cb)` 异步查询：
     - 命中 → `adviceCb(result::error(UNAUTHORIZED, "token revoked"))` 短路；
     - 未命中 → 把 JWT 载荷写入 `req->attributes()["user"]`，`chainCb()` 放行。

> ⚠️ API 约定：**所有业务响应 HTTP 恒为 200**，状态码在 JSON body 的 `code` 字段（`result::make` 恒设 `k200OK`）。因此「token 已失效」表现为 **HTTP 200 + body `{"code":401,"message":"token revoked"}`**，而非 HTTP 401。测试与前端须查 body `code`。

### 5.4 fail-open（关键）

`isBlacklisted` 使用 `execCommandAsync`（非协程阻塞），回调内：
- 正常返回 → 按结果 `cb(true/false)`；
- **异常 / 超时**（`kTimeout` / `Connection broken` / `Network failure`）→ `cb(false)` **放行** + `LOG_ERROR "auth blacklist query failed (fail-open)"`。

配合 `timeout=1.0`，Redis 不可用时鉴权在 ~1s 内 fail-open，请求不悬挂、不崩溃。安全性权衡：Redis 宕机期间丧失「登出即失效」能力，但 token 仍受签名与过期保护。

### 5.5 TTL 自清理

`blacklist(jti, ttl)` 写入 `SET auth:blacklist:{jti} 1 PX {ttl*1000}`，TTL = token 剩余有效期。Redis 自动过期，黑名单不会无限增长，也无需手工清理（最长存活 = access token 最大剩余 24h）。

## 6. 能力三：Redis Pub/Sub 实时推送

### 6.1 动机

原 `WsHub` 用进程内 `map<conn, topics>` 广播，仅限单实例。为后续水平扩展（多后端实例共享推送）与模块解耦，改用 **Redis Pub/Sub 作传输骨干**：生产者 `broadcast` → `PUBLISH`，各实例 `psubscribe` 收到后投递给本地 WS 连接。当前单实例部署下同样受益（广播与投递解耦）。

### 6.2 WsHub 机制（[ws_hub.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/ws/ws_hub.cpp)）

- `init()`（main.cpp 启动期调用）：`redis::client()->newSubscriber()` → `psubscribe("ibmcp:ws:*")`；消息回调解析 channel（`ibmcp:ws:<topic>`），调 `deliverLocal(topic, msg)` 投递给**本地**订阅该 topic 的连接。
- `broadcast(topic, msg)`：`execCommandAsync("PUBLISH %s %s", "ibmcp:ws:<topic>", msg)`，fire-and-forget（失败仅 `LOG_ERROR`，不阻塞调用方）。
- `subscribe` / `unsubscribe`：仅维护本地 `subscriptions_[conn]` 集合，**不**向 Redis 注册（Redis 只做跨实例传输骨干，实例内靠本地 map 过滤）。
- 订阅确认：客户端订阅后服务端下发 `{"topic":...,"type":"subscribed"}` / `unsubscribed`，前端协议兼容。

### 6.3 主题与调用方

| channel | 触发方 | 载荷（JSON 含 `type` 字段） |
|---------|--------|--------------------------|
| `ibmcp:ws:data` | 采集器 `sample_pump` 每 tick | 最新采集数据（无设备时 tick 早退不广播，属既有行为） |
| `ibmcp:ws:alarm` | 告警事件分发器 | 告警产生 / 状态变更 |
| `ibmcp:ws:device` | 设备上下线 / 运行态变化 | 设备状态 |
| `ibmcp:ws:control` | 控制指令下发 / 回执 | 控制结果 |

> [sample_pump.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/collector/sample_pump.cpp) 的 `broadcast` 调用点**零改动**（`void broadcast(const std::string&, const Json::Value&)` 签名不变），传输层由进程内 → Redis Pub/Sub 透明替换，前端 WS 消息格式完全兼容。

### 6.4 降级

- `init()` 失败仅 `LOG_ERROR`，不阻塞启动；`broadcast` 失败仅记日志，不影响 HTTP。
- 订阅连接断连由 Drogon / hiredis 自动重连；Redis 恢复后推送链路自愈。

## 7. 键空间总览

| 键模式 | 示例 | 用途 | 过期 | 生产者 | 消费者 |
|--------|------|------|------|--------|--------|
| `cache:*` | `cache:static_dict:bootstrap` | 响应缓存 | 5–600s | 各读控制器回写 | 各读控制器 |
| `auth:blacklist:{jti}` | `auth:blacklist:3bdc...316` | 登出 token 黑名单 | = token 剩余 | `logout` | 全局鉴权 advice |
| `ibmcp:ws:<topic>`（pub/sub，非存储） | `ibmcp:ws:data` | WS 推送通道 | — | `WsHub::broadcast` / 采集器 | `WsHub::psubscribe` → 本地 WS |

## 8. 故障降级与影响面

**场景：Redis 容器宕机**（`docker stop ibmcp-redis`）

| 路径 | 表现 | 是否符合预期 |
|------|------|-------------|
| 缓存读接口 | 回源 PG，首屏慢（get+回写各 ~1s 超时叠加 ≈ 2–3s），HTTP 200 有数据 | ✅ fail-open，不崩不 500 |
| 鉴权 | 有效 token ~1s 内放行（body code 0），丧失登出即失效 | ✅ fail-open |
| WS 推送 | `broadcast` 仅记日志，无新消息；`/api/v1/ping` 200 | ✅ 不阻塞 HTTP |
| 进程 | `systemd` MainPID 不变，无重启 / 崩溃 | ✅ |

**恢复**：`docker start ibmcp-redis` 后 Drogon 客户端自动重连，缓存 / 订阅 / 黑名单检查自动恢复（缓存键在首次访问时重建）。

## 9. 运维与观测

- 健康探针：`GET /api/v1/redis/ping` → `{"data":{"ping":"PONG","dbsize":"N",...}}`。
- `redis-cli` 快速检查：
  ```bash
  docker exec ibmcp-redis redis-cli EXISTS cache:static_dict:bootstrap
  docker exec ibmcp-redis redis-cli TTL    cache:static_dict:bootstrap
  docker exec ibmcp-redis redis-cli --scan --pattern 'cache:*' | head
  docker exec ibmcp-redis redis-cli PSUBSCRIBE ibmcp:ws:*     # 观测推送流
  ```
- 日志关键字（`dist/logs/ibmcp.log`）：
  - `cache get failed` / `cache set failed` — 缓存异常（fail-open 已回源）
  - `auth blacklist query failed (fail-open)` — 黑名单查询失败放行
  - `WS publish failed` — 推送发布失败
  - `Command execution timeout` — Redis 命令超时（触发 fail-open）
- 清理：全部缓存键带 TTL、黑名单键 TTL = token 剩余期，**无需手工清理**；如需全量刷新可 `docker exec ibmcp-redis redis-cli --scan --pattern 'cache:*' | xargs docker exec -i ibmcp-redis redis-cli DEL`。

## 10. 测试

端到端回归脚本 [.trae/specs/redis-advanced-features/ac_test.py](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/.trae/specs/redis-advanced-features/ac_test.py) 覆盖 28 项断言（在 openEuler 真实部署运行）：

- 缓存命中字节一致 + TTL 合法（`C1*`）；
- 写时失效：字典 / 配置写后对应键 EXISTS → 0（`C2*`、`C3`）；
- 登出失效 + 黑名单 TTL > 0 且对齐剩余有效期（`T1`/`T2`）；
- 被拉黑 token 被拒（body code 401 `token revoked`）、新 token 不受影响（`T3`）；
- route expand 新鲜度：device 写后 expand 键被失效（`C2e`，防回归审查 Finding 1）。

降级验证（停 Redis 后各路径 fail-open）与 WS 推送抓包证据见 [.trae/specs/redis-advanced-features/tasks.md](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/.trae/specs/redis-advanced-features/tasks.md) Task 9 与 [review.md](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/.trae/specs/redis-advanced-features/review.md)。
