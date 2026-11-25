# IBMCP Redis 进阶能力 - 实施计划

> 依赖：`ibmcp::redis::client()` 访问器、`jwt::verifyToken/createToken`、`WsHub` 均已存在（基础接入已完成并验证）。

## Task 1: 缓存工具层 `ibmcp::cache`
- **Status**: `completed`
- **Priority**: high
- **Depends On**: None
- **Completion Evidence**:
  - 新建 [backend/include/core/cache.h](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/include/core/cache.h) + [backend/src/core/cache.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/core/cache.cpp)，实现 `getJson`/`setJson`/`invalidate`/`getOrSet` 模板，key 前缀常量 `cache::kPrefix = "cache:"`，值紧凑 JSON。
  - 底层走 `redisvFormatCommand`（printf 风格），`%s` 传 `.c_str()`、`%d` 传 int，协程帧保活局部字符串，生命周期安全。
  - Redis 异常全部降级：get→nullopt，set/invalidate→仅 LOG_ERROR。`client()` 为 nullptr 时直接降级。
  - 欧拉 `build.sh` 0 error 编译通过（含新文件被 CMake GLOB 收录）。端到端命中/降级由 Task 9 验证。
- **Description**:
  - 新建 `backend/include/core/cache.h` + `backend/src/core/cache.cpp`，封装对 Redis 的 JSON 缓存访问：
    - `Task<std::optional<Json::Value>> getJson(const std::string &key)`
    - `Task<void> setJson(const std::string &key, const Json::Value &val, int ttlSec)`
    - `Task<void> invalidate(const std::string &key)`
    - 模板辅助 `template<class Loader> Task<Json::Value> getOrSet(const std::string &key, int ttlSec, Loader &&loader)`：命中返回缓存；未命中或 Redis 读异常 → 执行 loader（回源）→ 回填（失败仅记日志）→ 返回。
  - key 前缀统一 `cache:`；值用紧凑 JSON 字符串存储（`Json::writeString` 无缩进）。
  - Redis 异常一律捕获并降级：get 失败视为未命中，set/invalidate 失败仅 LOG_ERROR 不抛。
- **Acceptance Criteria Addressed**: AC-C1, AC-C4
- **Test Requirements**:
  - `rule` TR-1.1: 欧拉端 `build.sh` 编译 0 error；调用一个走 getOrSet 的接口后 `redis-cli GET cache:<key>` 存在且为合法 JSON。证据：build 输出 + redis-cli。
  - `rule` TR-1.2: Redis 不可用时 getOrSet 走 loader 不抛异常、接口仍 200（由 AC-C4 端到端验证）。证据：停 redis 容器后 curl。

## Task 2: 在读接口接入缓存
- **Status**: `completed`
- **Priority**: high
- **Depends On**: Task 1
- **Completion Evidence**:
  - 已在 8 个控制器用 `cache::getOrSet` 包裹回源逻辑：static_dict（bootstrap/all/byCode/byId/page）、config_device/list、config_route（list/expand）、config_function/list、config_point/list、data/latest、runtime_device（list/overview）。
  - key 包含全部影响输出的参数（route_id/config_id/binding_id/page/size/status/keyword/filters/point_ids）。
  - TTL：static_dict 600、config 列表 300、data/latest 5、runtime 30。data/history 未缓存。
  - 缓存读取在 `auth::requireAuth` 之后。命中/未命中均经 `result::ok(data)` 包装，返回体结构一致。
  - 欧拉 build.sh 0 error 通过。端到端命中与字节一致性由 Task 9 验证。
- **Description**:
  - 在以下控制器的 GET 处理函数中，用 `cache::getOrSet` 包裹数据库查询，缓存 `result::ok` 的 `data` 部分（命中直接返回，跳过查库）：
    - `static_dict_controller`：bootstrap / all / byCode / byId / page → key `cache:static_dict:bootstrap`、`cache:static_dict:{dict}:all`、`cache:static_dict:{dict}:code:{code}`、`cache:static_dict:{dict}:{id}`、`cache:static_dict:{dict}:page:{page}:{size}`；TTL 600。
    - `config_device_controller::list` → `cache:config:devices`，TTL 300。
    - `config_route_controller::list` → `cache:config:routes`；`expand` → `cache:config:routes:{id}:expand`，TTL 300。
    - `config_function_controller::list` → `cache:config:functions`，TTL 300。
    - `config_point_controller::list` → `cache:config:points`，TTL 300。
    - `data_controller::latest` → `cache:data:latest:{deviceCode}:{pointIds}`（pointIds 为空时用 `_` 占位），TTL 5。
    - `runtime_device_controller::list` → `cache:runtime:devices`；`overview` → `cache:runtime:devices:overview`，TTL 30。
  - key 必须包含所有影响输出的查询参数。
  - 缓存读取放在鉴权之后（这些接口返回与用户无关，无越权风险）。
- **Acceptance Criteria Addressed**: AC-C1, AC-C3, AC-C5, AC-C7
- **Test Requirements**:
  - `rule` TR-2.1: 对每个缓存接口，首次请求后 `redis-cli GET <key>` 存在；第二次相同请求返回体与首次逐字节一致（diff 为空）。证据：curl 两次 + diff + redis-cli。
  - `rule` TR-2.2: data/latest 传不同 point_ids 产生不同 key（`redis-cli KEYS cache:data:latest:*` 至少 2 个且值不同）。证据：redis-cli KEYS。
  - `rubric` TR-2.3: 缓存接入的侵入性；scale 1-5；1=每个控制器复制大量样板；3=有重复但可读；5=全部经 getOrSet 单行包裹、控制器逻辑清晰；threshold >= 4。证据：控制器 diff 审查。

## Task 3: 写时失效
- **Status**: `completed`
- **Priority**: high
- **Depends On**: Task 2
- **Completion Evidence**:
  - 新增 `cache::invalidateMatch(pattern)`（SCAN+DEL）覆盖带参数的列表变体；`invalidate(key)` 用于单 key。
  - 失效映射：static_dict 写→删 bootstrap + `{dict}:all`；config_device 写→`cache:config:devices*`；config_route 写→`cache:config:routes*`；config_function 写→`cache:config:functions*`；config_point 写→`cache:config:points*`；control/write（含 runtime control）→`cache:runtime:devices*`。
  - 失效调用在写成功后、返回前；异常由 cache 层吞掉仅记日志，不影响写接口返回。
  - 欧拉 build.sh 0 error 通过。端到端失效验证由 Task 9 完成。
- **Description**:
  - 在对应写接口成功提交后调用 `cache::invalidate(key)`：
    - static_dict create/update/remove → invalidate `cache:static_dict:bootstrap` 与 `cache:static_dict:{dict}:all`（page/byId/byCode 靠 TTL 过期，在 spec 中已声明）。
    - config_device create/update/remove → invalidate `cache:config:devices`。
    - config_route create/update/remove → invalidate `cache:config:routes`；update/remove 还 invalidate `cache:config:routes:{id}:expand`。
    - config_function create/update/remove → invalidate `cache:config:functions`。
    - config_point create/update/remove → invalidate `cache:config:points`。
    - control_controller::write 成功 → invalidate `cache:runtime:devices` 与 `cache:runtime:devices:overview`。
  - invalidate 失败不影响写接口返回（fail-safe）。
- **Acceptance Criteria Addressed**: AC-C2
- **Test Requirements**:
  - `rule` TR-3.1: `cache:config:devices` 已存在时，调用 device create/update/remove 任一后 `redis-cli EXISTS cache:config:devices` = 0；再次 GET 列表回源。证据：curl 写 + redis EXISTS + 再 GET。
  - `rule` TR-3.2: static_dict 写后 `cache:static_dict:bootstrap` 与对应 `cache:static_dict:{dict}:all` 被删。证据：redis EXISTS。
  - `rule` TR-3.3: 调用 `/api/v1/control/write` 后 runtime 两个 key 被删。证据：redis EXISTS。

## Task 4: WsHub 改造为 Redis Pub/Sub 骨干
- **Status**: `completed`
- **Priority**: high
- **Depends On**: None
- **Completion Evidence**:
  - [backend/include/ws/ws_hub.h](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/include/ws/ws_hub.h) + [backend/src/ws/ws_hub.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/ws/ws_hub.cpp)：新增 `init()` 持 `RedisClientPtr redis_` + `shared_ptr<RedisSubscriber> sub_`，psubscribe `ibmcp:ws:*`。
  - 抽出 `deliverLocal(topic, msg)`；`broadcast` 改为序列化紧凑 JSON 后 `PUBLISH ibmcp:ws:<topic>`（execCommandAsync fire-and-forget，保持同步签名，sample_pump 零改动）。
  - 单实例 publish→订阅回调→deliverLocal，无重复投递；投递给 WS 的字节 = payload 紧凑 JSON（与改造前一致）。
  - redis 不可用时 init 记 ERROR 跳过、broadcast 记 ERROR 跳过，不崩溃。
  - 欧拉 build.sh 0 error 通过。
- **Description**:
  - 修改 `backend/include/ws/ws_hub.h` + `backend/src/ws/ws_hub.cpp`：
    - 新增成员：`std::shared_ptr<nosql::RedisSubscriber> sub_`、`std::shared_ptr<nosql::RedisClient> redis_`。
    - 新增 `void init()`：从 `redis::client()` 取客户端，`newSubscriber()`，`psubscribe("ibmcp:ws:*", cb)`；cb 解析 channel 后缀为 topic，将 message 原样投递给本地订阅该 topic 的连接（抽出现有 `broadcast` 中的本地投递逻辑为 `deliverLocal(topic, msg)`）。
    - `broadcast(topic, payload)`：序列化 payload 为紧凑 JSON，调用 `redis_->execCommandAsync(okCb, errCb, "PUBLISH %s %s", "ibmcp:ws:<topic>", json)`，fire-and-forget（保持 `broadcast` 同步签名，`sample_pump` 调用方零改动）。errCb 仅 LOG_ERROR。
    - `sub_` 必须由 WsHub 持有保活。
  - 单实例：publish → 本实例订阅回调 → 本地投递，**无重复**（broadcast 不再直接本地投递）。
  - 投递给 WS 客户端的消息字节 = payload 的紧凑 JSON（与改造前 `broadcast` 写出的完全一致）。
- **Acceptance Criteria Addressed**: AC-P1, AC-P2, AC-P4
- **Test Requirements**:
  - `rule` TR-4.1: `redis-cli PSUBSCRIBE ibmcp:ws:*` 能观测到 broadcast 发出的消息（channel + message）。证据：redis-cli 输出。
  - `rule` TR-4.2: 单实例 1 个 WS 客户端订阅某 topic，触发 N 次 broadcast 恰好收到 N 条。证据：WS 测试客户端计数。
  - `rule` TR-4.3: `sample_pump.cpp` 零改动（`git diff --stat` 不含该文件），全量编译通过。证据：git diff + build。
  - `rubric` TR-4.4: 前端消息兼容性；scale 1-5；1=格式变化前端收不到；3=兼容但字段有增删；5=发出的 JSON 字节与改造前一致、前端 dispatch 正常；threshold = 5（必须严格一致）。证据：抓包/WS 客户端收到的 `type` 字段正确、前端能 dispatch。

## Task 5: WsHub 初始化与 Redis 故障降级
- **Status**: `completed`
- **Priority**: medium
- **Depends On**: Task 4
- **Completion Evidence**:
  - [backend/src/main.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/main.cpp) beginningAdvice 中 `ctx.init()` 后、collector bootstrap 前调 `ibmcp::ws::WsHub::instance().init()`（事件循环线程，先于采集器广播）。
  - init 内 try/catch + redis_ null 检查，失败仅 LOG_ERROR/LOG_INFO，不阻止启动。
  - 欧拉 build.sh 0 error 通过。日志与降级端到端由 Task 9 验证。
- **Description**:
  - 在 `backend/src/main.cpp` 的 `registerBeginningAdvice` 中（app 已配置、redis client 可用）调用 `ws::WsHub::instance().init()`；订阅成功/失败写 INFO 日志。
  - init 失败（redis 不可用）时 catch 异常、LOG_ERROR，不阻止服务启动；后续 broadcast 的 execCommandAsync 失败也仅记日志。
- **Acceptance Criteria Addressed**: AC-P3
- **Test Requirements**:
  - `rule` TR-5.1: 启动日志含 WS 订阅初始化的 INFO（成功或失败均可，服务必须起来）。证据：tail 日志。
  - `rule` TR-5.2: 停掉 redis 容器后触发一次 broadcast（如触发采集器数据），进程不崩溃，`/api/v1/ping` 返回 200。证据：停容器→触发→curl。

## Task 6: JWT 增加 jti + 黑名单工具
- **Status**: `completed`
- **Priority**: high
- **Depends On**: None
- **Completion Evidence**:
  - [backend/src/core/jwt_util.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/core/jwt_util.cpp) `createToken` 写入 `jti`（`<random>` 生成 128-bit hex，每次唯一）。
  - 新建 [backend/include/core/auth_redis.h](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/include/core/auth_redis.h) + [backend/src/core/auth_redis.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/core/auth_redis.cpp)：`isBlacklisted(jti, cb)`（async，fail-open）、`isBlacklistedCoro`、`blacklist(jti, ttl)`（SET EX，ttl≤0 不写）。key 前缀 `auth:blacklist:`。
  - 欧拉 build.sh 0 error 通过。端到端黑名单由 Task 9 验证。
- **Description**:
  - `backend/src/core/jwt_util.cpp` 的 `createToken`：在 payload 写入 `jti`（生成 UUID，可用 openssl 或 `<random>` 拼 128bit 十六进制），每次签发唯一。
  - 新建 `backend/include/core/auth_redis.h` + `backend/src/core/auth_redis.cpp`：
    - `void isBlacklisted(const std::string &jti, std::function<void(bool)> cb)`：用 `execCommandAsync` 查 `EXISTS auth:blacklist:<jti>`，cb(true) 表示命中；Redis 异常 cb(false)（fail-open）。
    - `Task<void> blacklist(const std::string &jti, int64_t ttlSec)`：`SET auth:blacklist:<jti> 1 EX <ttl>`（ttl≤0 时不写）。
    - `Task<bool> isBlacklistedCoro(const std::string &jti)`：协程版（供登出等协程上下文可选使用，非必须）。
- **Acceptance Criteria Addressed**: AC-T1, AC-T2
- **Test Requirements**:
  - `rule` TR-6.1: 新签发的两个 token 解码后 payload 均含 `jti`，且两个 jti 不同。证据：base64 解码 token payload。
  - `rule` TR-6.2: `blacklist(jti, 60)` 后 `redis-cli EXISTS auth:blacklist:<jti>` = 1 且 `TTL` ∈ (0,60]；随后 isBlacklisted 回调为 true。证据：redis-cli + 测试调用。

## Task 7: 鉴权链路接入黑名单检查
- **Status**: `completed`
- **Priority**: high
- **Depends On**: Task 6
- **Completion Evidence**:
  - [backend/src/main.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/main.cpp) 全局 post-routing advice（L54-101）：`verifyToken` 通过后取 `jti`；为空→写 user 属性并 `chainCb()`（兼容存量旧 token 至自然过期）；非空调 `auth::isBlacklisted(jti, cb)`，cb 内命中→`adviceCb(result::error(UNAUTHORIZED, "token revoked"))`，未命中→写 user 并 `chainCb()`。Drogon advice 异步安全（仅在回调调用时推进链，函数返回不自动放行），`execCommandAsync` 模式正确。
  - [backend/src/filter/auth_filter.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/filter/auth_filter.cpp)（L40-56）同步加入相同黑名单逻辑（当前未被控制器注册，保持一致防后续启用）。
  - fail-open：`isBlacklisted` 内部 `execCommandAsync` 异常→`cb(false)` 放行并 LOG_ERROR；jti 缺失放行。
  - 欧拉 build.sh 0 error。端到端由 Task 9 验证：登出后受保护接口 body code=401 `token revoked`；停 redis 后有效 token 仍 200（fail-open，日志含 `auth blacklist query failed (fail-open): Command execution timeout`）。
- **Description**:
  - `backend/src/main.cpp` 全局 post-routing advice：`verifyToken` 通过后，取 `payload["jti"]`；若为空直接放行（向后兼容存量旧 token）；否则调 `auth::isBlacklisted(jti, cb)`，cb 内：命中 → `adviceCb(result::error(UNAUTHORIZED, "token revoked"))`；未命中 → 写入 user 属性并 `chainCb()`。Redis 异常路径由 isBlacklisted 内部 fail-open（cb(false)）。
  - `backend/src/filter/auth_filter.cpp` 同步加入相同逻辑（当前未启用，保持一致防后续启用）。
  - 失败信息与日志：黑名单命中写 INFO，Redis 查询异常写 ERROR 但放行。
- **Acceptance Criteria Addressed**: AC-T1, AC-T3, AC-T4
- **Test Requirements**:
  - `rule` TR-7.1: token 登出后，用该 token 访问 `/api/v1/auth/me` 返回 401 且 message 含 `revoked`。证据：curl。
  - `rule` TR-7.2: 另一用户未登出的 token 访问 `/api/v1/auth/me` 仍 200。证据：curl 对照。
  - `rule` TR-7.3: 停 redis 容器后，有效 token 访问受保护接口仍 200，日志含 redis 异常告警。证据：停容器→curl→tail 日志。
  - `rule` TR-7.4: `auth_filter.cpp` 中存在 isBlacklisted 调用。证据：grep。

## Task 8: 登出端点
- **Status**: `completed`
- **Priority**: high
- **Depends On**: Task 6, Task 7
- **Completion Evidence**:
  - [backend/src/controller/auth_controller.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/auth_controller.cpp) 新增 `Task<HttpResponsePtr> AuthController::logout`（L103-137），路由 `POST /api/v1/auth/logout`（走正常鉴权，未加入 public_paths）。
  - 从 `Authorization: Bearer` 取 access token，`verifyToken` 取 `jti`/`exp`，`ttl = exp - now`，`co_await auth::blacklist(jti, ttl)`（`blacklist` 内部 ttl≤0 时不写）。
  - 请求体含 `refresh_token` 时：`verifyToken` 校验通过取其 jti/exp，同样 `blacklist`（防被用来换新 token）。
  - 返回 `result::ok()`（body code 0）。
  - 欧拉 build.sh 0 error。端到端：logout 返回 200 code 0；`EXISTS auth:blacklist:<jti>`=1，`TTL`=86396（>0 且≤token 剩余有效期）；随后该 token 访问受保护接口 body code=401 `token revoked`。
- **Description**:
  - `auth_controller.h` + `.cpp` 新增 `POST /api/v1/auth/logout`（无需加入 public_paths，走正常鉴权）：
    - 从请求属性 `user` 取已校验 payload 的 `jti` 与 `exp`；`ttl = max(0, exp - now)`；`co_await auth::blacklist(jti, ttl)`。
    - 若请求体含 `refresh_token`：`verifyToken` 校验通过且 `scope==refresh`，取其 jti/exp，同样 blacklist。
    - 返回 `result::ok()`。
- **Acceptance Criteria Addressed**: AC-T1, AC-T2
- **Test Requirements**:
  - `rule` TR-8.1: `POST /api/v1/auth/logout`（带 Bearer）返回 `code:0`。证据：curl。
  - `rule` TR-8.2: 登出后 `redis-cli TTL auth:blacklist:<jti>` > 0 且 ≤ token 原始剩余秒数。证据：redis-cli TTL。

## Task 9: 部署 + 全量验证
- **Status**: `completed`
- **Priority**: high
- **Depends On**: Task 3, Task 5, Task 7, Task 8
- **Completion Evidence**:
  - **部署**：源码 scp 到 `/home/meng/ibmcp`（scp 保留 mtime → 每次改源后 `touch` 强制重编，避免 CMake 跳过重编），`deployments/openeuler/build.sh` 0 error，产物 `/home/meng/ibmcp/dist/bin/ibmcp_server`，`sudo systemctl restart ibmcp-api` 后 active，`/api/v1/redis/ping` → PONG。
  - **验证中发现并修复的问题**：
    1. `config.json` `redis_clients[].timeout` 原为 `-1.0`（无界）→ 停 redis 时缓存读/鉴权命令排队等重连，挂约 36s 才失败，应用形同不可用。改为 `1.0`（命令超时 1s 即 fail-open），正常命令亚毫秒不受影响。已同步到 `dist/config.json` 并重启生效。Drogon `ConfigLoader::loadRedisClients` 证实该字段映射到 `createRedisClient` 的 timeout 参数。
    2. 测试脚本两处误报修正（持久化到 [.trae/specs/redis-advanced-features/ac_test.py](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/.trae/specs/redis-advanced-features/ac_test.py)）：本 API 所有业务响应 HTTP 恒 200、状态在 body `code`（`result::make` 恒设 k200OK），故 T3 断言查 body code==401（非 HTTP 401）；C3 的 PUT 前剥掉只读字段 `id`/`created_at`/`updated_at`（否则被 422 拒绝但 HTTP 仍 200，造成"update 成功"假通过、失效没触发）。
  - **AC 端到端结果（全量 24/24 PASS，config 改动后复跑仍 24/24）**：
    - AC-C1 缓存命中：bootstrap 两次响应字节一致（len=10733），`cache:static_dict:bootstrap` 存在，TTL 599 ∈ (0,600]。
    - AC-C2 写时失效：static_dict update（剥只读字段后 body code 0）后 `cache:static_dict:bootstrap` 与 `cache:static_dict:brands:all` EXISTS 均变 0；data/history 不缓存、data/latest 缓存。
    - AC-C3 TTL：TTL 599 ∈ (0,600]，到期自然回源（由 TTL 机制保证）。
    - AC-C4 缓存故障降级：停 redis 后 bootstrap `time=2.9s` HTTP 200 body code 0 有数据（get+回写各超时 1s 叠加，已收敛回源、不抛 500）；恢复 redis 后 key 重建 EXISTS=1。
    - AC-P1/P2 WS 推送：`redis-cli PSUBSCRIBE ibmcp:ws:*` 观测到消息流；Python 原生 WS 客户端连 `/ws/push` 订阅 data/device，手动 `PUBLISH ibmcp:ws:data`/`alarm` 均被投递，JSON 含 `type` 字段（前端兼容格式）；3 次 publish 恰好收 3 条业务帧 + 2 条订阅确认，无重复投递。`sample_pump.cpp` 零改动（`git diff` 为空）。
    - AC-P3 推送/HTTP 解耦：停 redis 后 `/api/v1/ping` 0.001s 200，MainPID 不变（无重启/崩溃），broadcast 仅记日志；redis 重启后订阅自动重连、缓存重生。
    - AC-T1 登出失效：logout 200 code 0；随后受保护接口 body code=401 message `token revoked`；`EXISTS auth:blacklist:<jti>`=1。
    - AC-T2 TTL 对齐：blacklist TTL=86396 > 0 且 ≤ token 剩余有效期，Redis 自清理。
    - AC-T3 不误伤：同账号新登录 token 仍 200（黑名单按 jti 精准命中）。
    - AC-T4 鉴权 fail-open：停 redis 后有效 token verify `time=0.94s` HTTP 200 body code 0 valid=true，日志 `auth blacklist query failed (fail-open): Command execution timeout`。
    - AC-X1 全量回归：24/24 无 5xx，核心接口响应结构不变。
  - **清理确认**：源码无 `[diag]` 诊断日志残留（`strings` 二进制 0 匹配），本地与欧拉部署一致。
- **Description**:
  - 同步全部改动到欧拉端，`build.sh` 编译，`systemctl restart ibmcp-api`。
  - 依次执行 AC-C1~AC-X1 的端到端验证（含停/启 redis 容器的降级测试），输出 PASS/FAIL 汇总。
  - 修复验证中发现的问题。
- **Acceptance Criteria Addressed**: AC-C1, AC-C2, AC-C3, AC-C4, AC-P1, AC-P2, AC-P3, AC-T1, AC-T2, AC-T3, AC-T4, AC-X1
- **Test Requirements**:
  - `rule` TR-9.1: `build.sh` 0 error；服务 active；核心接口回归无 5xx。证据：build 输出 + 回归脚本。
  - `rule` TR-9.2: 所有 AC 端到端测试通过（汇总表）。证据：测试脚本输出。
  - `rubric` TR-9.3: 整体健壮性；scale 1-5；1=存在重复投递/误伤/降级崩溃；3=主流程可用但边缘瑕疵；5=三能力均健壮、降级正确、无回归；threshold >= 4。证据：全量测试 + 日志审查。
