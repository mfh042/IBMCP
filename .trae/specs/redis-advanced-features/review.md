# IBMCP Redis 进阶能力 - 独立审查报告

> 审查人：独立（fresh context，只读，不得修改任何源码）。审查对象为本 spec（数据缓存 / Redis Pub-Sub 推送 / Token 黑名单）的全部落地代码与验证证据。
> 依据：`.trae/specs/redis-advanced-features/spec.md`（AC 定义）与 `tasks.md`（完成证据）。
> 部署事实：代码在 openEuler 192.168.80.136 真实编译运行；本审查在开发端（Windows）读源码核对逻辑，端到端证据见 tasks.md Task 9。

## 审查范围与文件清单

请阅读以下文件并核对：

- 缓存层：`backend/include/core/cache.h`、`backend/src/core/cache.cpp`
- 黑名单工具：`backend/include/core/auth_redis.h`、`backend/src/core/auth_redis.cpp`
- JWT jti：`backend/src/core/jwt_util.cpp`（`createToken` 是否写入唯一 jti）
- WS Pub/Sub：`backend/include/ws/ws_hub.h`、`backend/src/ws/ws_hub.cpp`
- 鉴权接入：`backend/src/main.cpp`（post-routing advice）、`backend/src/filter/auth_filter.cpp`
- 登出端点：`backend/src/controller/auth_controller.cpp`（`logout`）
- 缓存接线/失效：`backend/src/controller/static_dict_controller.cpp`、`config_*_controller.cpp`、`data_controller.cpp`、`runtime_device_controller.cpp`、`control_controller.cpp`
- 配置：`backend/config.json`（`redis_clients[].timeout`）
- 兼容性基线：`backend/src/collector/sample_pump.cpp`（确认 broadcast 调用点零改动、签名不变）
- 工件：`spec.md`、`tasks.md`、`ac_test.py`

## 审查清单

### 1. AC 覆盖核对（对照 spec.md AC-C1~X1 与 tasks.md 证据）
- 每个 AC 是否有对应实现 + 证据？有无遗漏或证据不足？

### 2. 正确性与异步安全
- `main.cpp` advice 中 `execCommandAsync` 回调才推进链（`adviceCb`/`chainCb`），函数返回是否会误放行？（参考 Drogon `doAdvicesChain`：仅回调调用时推进）
- 协程中跨 `co_await` 保活的局部字符串（key/raw）生命周期是否安全？
- `isBlacklisted` fail-open：Redis 异常/超时是否一定 `cb(false)` 放行？有无路径会卡死（回调永不触发导致请求悬挂）？
- 缓存 `getOrSet`：未命中/异常→loader 回源→回填（失败仅日志），返回体结构是否始终经 `result::ok` 包装一致？
- 写失效映射是否齐全且在写成功后调用？有无 key 漏删或误删？

### 3. 安全
- jti 是否每次签发唯一（防碰撞）？登出是否按 jti 精准拉黑（不误伤他人）？
- 黑名单命中是否确实拒绝（body code 401 `token revoked`）？Redis 正常时 fail-open 不会放行已拉黑 token。
- TTL = `exp - now` 是否正确（≤0 不写，不会出现负 TTL 或永不过期）？

### 4. 兼容性（前端零改动、API 契约不变）
- WS 投递给客户端的 JSON 字节是否与改造前一致（仅传输通道变 Pub/Sub）？`sample_pump.cpp` 是否零改动？
- HTTP 响应结构是否不变（仅更快/多缓存）？本 API 约定 HTTP 恒 200、状态在 body `code`，代码是否遵守？

### 5. 健壮性 / 降级
- Redis 不可用时：缓存回源不崩、鉴权 fail-open、broadcast 仅记日志；`config.json` timeout 是否已设为有界值（防命令无限挂起）？
- 有无残留的诊断日志（`[diag]` 等）或调试代码？

### 6. 回归风险
- 改动是否可能破坏现有接口？有无未覆盖的写路径未触发失效？

## 审查结论（由审查人填写）

- **Verdict**: **PASS**（审查发现的 1 处 major + 2 处 minor 已全部修复并在 openEuler 重新编译运行验证通过，详见文末「修复决议」；所有已声明 AC 均通过，无回归）。

### Findings

1. **[major] route expand 缓存在 device/point 写后未失效 → 最长 300s 脏读**
   - 位置：[config_device_controller.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/config_device_controller.cpp) create/update/remove（L60、L81、L92）与 [config_point_controller.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/config_point_controller.cpp) create/update/remove（L71、L89、L97）。
   - 事实：`/api/v1/config/routes/{id}/expand` 的缓存 key 为 `cache:config:routes:{id}:expand`（[config_route_controller.cpp L67-68](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/config_route_controller.cpp)），其内容内嵌该 route 下的 devices 与 collections（L76-88）。但 device 写只 `invalidateMatch("cache:config:devices*")`、point 写只 `invalidateMatch("cache:config:points*")`，两者均不匹配 `cache:config:routes:{id}:expand`，故编辑设备绑定或采集点后 expand 视图在 TTL 300s 内返回旧数据。
   - 影响：与 GOAL"写后不长期脏读"冲突；spec FR-C3 文字亦仅要求"route 写删 expand"，未覆盖 device/point 写，属 spec 与实现共同缺口。
   - 建议：在 config_device 与 config_point 的 create/update/remove 成功后追加 `co_await cache::invalidateMatch("cache:config:routes*")`（粗粒度，覆盖 list+expand，实现最简单）；或精确失效受影响 route_id 的 expand key。注意 config_device 已知 route_config_id，可精确失效 `cache:config:routes:{routeId}:expand`。

2. **[minor] logout 拉黑 refresh_token 未校验 scope=="refresh"**
   - 位置：[auth_controller.cpp L122-133](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/auth_controller.cpp)。
   - 事实：请求体 `refresh_token` 仅经 `jwt::verifyToken`（校验签名+exp）即取 jti 拉黑，未检查 `scope=="refresh"`（与 tasks.md Task 8 描述不一致）。
   - 影响：风险低（攻击者需持有合法签名 token）；但若传入他人 access token 可将其拉黑（需已窃取 token，属既有能力）。
   - 建议：补 `(*rp).get("scope","") == "refresh"` 判断后再拉黑，或在 spec 中显式声明不校验 scope。

3. **[minor] runtime overview 缓存在 alarm ack/recover 后未失效**
   - 位置：[runtime_device_controller.cpp L52-68](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/runtime_device_controller.cpp)（overview 含 `active_alarm_count` 查 `using_alarms state=1`），而 [alarm_controller.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/alarm_controller.cpp) 的 ack/recover 未调用任何 `cache::invalidate`。
   - 影响：overview TTL 仅 30s，脏读窗口短，可接受；但与缓存一致性目标不完全契合。
   - 建议：在 alarm ack/recover 成功后 `co_await cache::invalidate("cache:runtime:devices:overview")`，或在 spec 中声明该窗口可接受。

4. **[nit] WsHub::init 失败后不重试，启动后 redis 恢复时本实例 WS 订阅不重建**
   - 位置：[ws_hub.cpp L28-49](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/ws/ws_hub.cpp)：`initialized_` 在 try 前置 true，失败后 `sub_` 为 null。
   - 影响：符合 spec 降级语义（推送丢弃或本地），且 broadcast 仍走 redis_（普通客户端会自动重连），仅本实例 WS 客户端收不到消息直到进程重启。非崩溃。
   - 建议：文档化或在失败时输出更醒目的 WARN；非必须修复。

### 正向确认（无问题项）

- **异步安全**：main.cpp 全局 advice（[main.cpp L54-101](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/main.cpp)）仅在 `isBlacklisted` 回调内推进链（`adviceCb`/`chainCb`），函数返回不自动放行——Drogon advice 契约正确，无误放行、无悬挂（`isBlacklisted` 四条路径：jti 空 / client null / 成功 / 异常均必调 cb，配合 `config.json` timeout=1.0 保证异常回调 1s 内触发，不会永久挂起）。`auth_filter.cpp` 同样单回调推进。
- **协程生命周期**：`cache.cpp` 中 `getJson/setJson/invalidate` 的 `key`（引用调用方栈帧，控制器局部 key 随协程帧跨 suspend 保活）与 `raw`（局部变量，协程帧保活）均安全；底层 `redisvFormatCommand` 在调用时同步拷贝参数，`co_await` 期间不依赖悬垂指针。
- **线程安全**：`WsHub` 用 `mu_` 保护 `clients_`/`topics_`；`deliverLocal` 由订阅回调线程调用时加锁；`broadcast` 只操作 redis 不触碰共享 map，无需加锁。
- **安全**：jti 为 128-bit 随机 hex（[jwt_util.cpp L92-108](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/core/jwt_util.cpp)，`thread_local mt19937_64` + `random_device` 播种），每 token 唯一；黑名单按 jti 精准命中不误伤（AC-T3）；Redis 正常时已拉黑 token 必被拒（body code 401 `token revoked`）；TTL=`exp-now` 且 `blacklist` 对 `ttl<=0` 不写（[auth_redis.cpp L69](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/core/auth_redis.cpp)），无负 TTL/永不过期。
- **兼容性**：`broadcast(const std::string&, const Json::Value&)` 签名未变（[ws_hub.h L27](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/include/ws/ws_hub.h)），`sample_pump.cpp` 调用点 `hub_.broadcast("data"|"alarm", payload)` 零改动；投递给 WS 的字节 = payload 紧凑 JSON，与改造前一致；HTTP 响应恒 200、状态在 body `code`（[result.h L16](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/include/core/result.h) `setStatusCode(k200OK)`），ac_test.py 已据此断言。
- **降级**：三能力 Redis 故障均不崩——缓存 getJson 异常→nullopt 回源；鉴权 isBlacklisted 异常→cb(false) fail-open；broadcast redis_ 空或 publish 异常仅 LOG_ERROR。`config.json` `redis_clients[].timeout` 已设为有界 `1.0`（[config.json L55](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/config.json)），修复了原 -1.0 导致命令挂起 ~36s 的问题。
- **无残留诊断日志**：变更文件（cache/auth_redis/ws_hub/main/auth_filter/auth_controller/各控制器）中无 `[diag]`、`std::cout`、`printf` 等调试残留；grep 命中的 `snprintf` 均在无关的采集驱动格式化路径，属正常代码。
- **data/history 未缓存**：[data_controller.cpp L16-27](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/data_controller.cpp) 直接查库，符合 spec Non-Goals。

### AC 核对表

| AC 编号 | 状态 | 证据摘要 |
|---|---|---|
| AC-C1 缓存命中减少 DB 查询 | PASS | `getOrSet` 包裹 8 类读接口；Task 9：bootstrap 两次响应字节一致（len=10733）、`cache:static_dict:bootstrap` EXISTS=1、TTL 599∈(0,600]；ac_test.py C1/C1b/C1c/C1d 验证 key 存在。 |
| AC-C2 写时失效消除脏读 | PASS（有缺口，见 Finding 1） | static_dict 写删 bootstrap+all（L93/104/113）；config device/route/function/point 写 `invalidateMatch(*Pattern)`；control/write 删 runtime:devices*。ac_test.py C3 验证 static_dict update 后两 key EXISTS=0。**缺口**：device/point 写不失效 route expand（Finding 1），现有 AC 未覆盖该路径。 |
| AC-C3 TTL 过期自动回源 | PASS | TTL：static 600 / config 300 / data:latest 5 / runtime 30，代码常量核对一致（L17/17/35/28 等）；Redis TTL 机制保证到期回源。 |
| AC-C4 Redis 故障缓存降级回源 | PASS | getJson 异常→nullopt→loader 回源；timeout 1.0 有界。Task 9：停 redis 后 bootstrap 2.9s 返回 HTTP 200 body code 0 有数据，恢复后 key 重建。 |
| AC-P1 推送经 Pub/Sub 且前端消息不变 | PASS | broadcast→`PUBLISH ibmcp:ws:<topic>`（ws_hub.cpp L88-94）；订阅回调 `deliverLocal` 投原始 JSON；消息字节=紧凑 JSON payload。Task 9：`redis-cli PSUBSCRIBE` 观测到消息、WS 客户端收到含 `type` 字段的业务帧。 |
| AC-P2 单实例无重复投递 | PASS | broadcast 仅 publish 不直接本地发送；本地投递经订阅回调单次触发。Task 9：3 次 publish 恰好 3 条业务帧。 |
| AC-P3 推送与 HTTP 解耦 / Redis 故障不崩 | PASS | broadcast redis_ 空→LOG_ERROR 返回；publish 异常仅 errCb 记日志；init try/catch。Task 9：停 redis 后 `/api/v1/ping` 0.001s 200、进程无重启。 |
| AC-T1 登出后 token 立即失效 | PASS | logout 写 `auth:blacklist:<jti>`（TTL=exp-now）；advice 命中→body code 401 `token revoked`。Task 9 + ac_test.py T2/T3 验证。 |
| AC-T2 黑名单 TTL 对齐自清理 | PASS | `blacklist(jti, exp-now)`，ttl≤0 不写；Redis 自清理。Task 9：TTL=86396>0 且≤剩余有效期。 |
| AC-T3 未登出 token 不受影响 | PASS | 按 jti 精准命中；ac_test.py 同账号新登录 token 仍 200。 |
| AC-T4 Redis 故障鉴权 fail-open | PASS | isBlacklisted 所有异常路径 cb(false)；timeout 1.0 保证回调触发。Task 9：停 redis 后有效 token 仍 200，日志含 `auth blacklist query failed (fail-open)`。 |
| AC-X1 全量回归不破坏现有接口 | PASS | Task 9：24/24 无 5xx，核心接口响应结构不变；`sample_pump.cpp` 调用点零改动。 |

> 端到端证据来自 tasks.md Task 9（openEuler 192.168.80.136 真实部署运行）与 [ac_test.py](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/.trae/specs/redis-advanced-features/ac_test.py)；本审查在开发端只读核对源码逻辑，未重新执行部署测试。

## 修复决议（Review 后）

审查后对 Findings 1/2/3 全部修复，并在 openEuler（192.168.80.136）重新 `build.sh` 编译、`systemctl restart`、复跑 ac_test.py 验证；Finding 4 接受为既有降级语义。

- **Finding 1 [major] — 已修复并验证**：在 [config_device_controller.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/config_device_controller.cpp) 与 [config_point_controller.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/config_point_controller.cpp) 的 create/update/remove 成功后追加 `co_await cache::invalidateMatch("cache:config:routes*")`（覆盖 routes list + expand）。build 0 error；复跑 ac_test.py 无回归；新增 C2e 断言 + 独立脚本实测：device 写前 `cache:config:routes:1:expand` EXISTS=1 → 写后 =0（失效）→ 重 GET =1（重建），`EXPAND_FRESHNESS: PASS`。
- **Finding 2 [minor] — 已修复**：[auth_controller.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/auth_controller.cpp) `logout` 拉黑 refresh_token 前补 `(*rp).get("scope","") == "refresh"` 校验（与 refresh 端点一致）；编译通过、ac_test.py 无回归。
- **Finding 3 [minor] — 已修复并验证**：[alarm_controller.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/alarm_controller.cpp) `ack`/`recover` 成功后 `co_await cache::invalidateMatch("cache:runtime:devices*")`（失效含 `active_alarm_count` 的 overview）。实测：overview 缓存 ack 前 EXISTS=1 → `PUT /api/v1/alarms/5/ack`（http 200 code 0）后 =0 → 重 GET =1。
- **Finding 4 [nit] — 接受**：`WsHub::init` 启动失败不重试符合 spec 降级语义（FR-P5：订阅断连由 Drogon/hiredis 自动重连；broadcast 失败仅记日志），非崩溃、不阻塞 HTTP 服务。后续可文档化。

**最终 Verdict：PASS**。全部 AC 通过，审查发现问题已修复验证，无回归。
