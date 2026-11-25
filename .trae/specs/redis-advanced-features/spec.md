# IBMCP Redis 进阶能力（数据缓存 / 实时推送 Pub-Sub / Token 黑名单）- 产品需求文档

## Overview
- **Summary**: 在已接通的 Redis 基础上，为 IBMCP 后端落地三项能力：①读接口数据缓存（TTL + 写时失效）；②将 WebSocket 实时推送改造为基于 Redis Pub/Sub 的广播骨干；③为无状态 JWT 增加 Token 黑名单，实现登出立即失效。
- **Purpose**: 降低高频读接口的数据库压力；让实时推送解耦生产端与 WS 连接、具备多实例横向扩展能力；补齐安全短板（登出后旧 token 立即可作废）。
- **Target Users**: 后端服务自身（缓存 / 推送）、前端浏览器（WS 订阅，零改动）、最终登录用户（登出失效）。

## Goals
- 高频读接口命中 Redis 缓存，降低 PG 读负载，且数据新鲜度可接受、写后不长期脏读。
- WS 实时推送经 Redis Pub/Sub 分发，前端协议与消息格式保持不变（零前端改动）。
- 用户登出后，其 access token（及可选 refresh token）立即被拒绝，即使未到自然过期时间。
- Redis 不可用时系统不崩溃：缓存回源数据库、推送降级为本地直推（或丢弃）、鉴权 fail-open 放行并记日志。

## Non-Goals
- 不做多级缓存（本地内存 + Redis），仅用 Redis。
- 不缓存 `/api/v1/data/history`（时间范围 + 聚合参数组合爆炸，性价比低）。
- 不改前端任何代码（WS 协议、订阅消息格式保持兼容）。
- 不引入有状态会话服务端存储（仍以 JWT 为主，仅加黑名单侧表）。
- 不做缓存预热、不做主动刷新任务。

## Background & Context
- Redis 已接入并验证通过（`/api/v1/redis/ping` 返回 `redis:ok / PONG`，容器 `DBSIZE` 可写），访问器 `ibmcp::redis::client()` 已就绪。
- Drogon 1.9.5 的 `nosql::RedisClient` 提供 `execCommandCoro`（协程）和 `execCommandAsync`（回调，适合非协程上下文）；`newSubscriber()` 产生 `std::shared_ptr<RedisSubscriber>`，`subscribe/psubscribe` 的回调签名为 `void(const std::string &channel, const std::string &message)`。
- 现有鉴权在 `backend/src/main.cpp` 的 `registerPostRoutingAdvice` 中执行（同步回调，非协程），调用 `jwt::verifyToken`；`backend/src/filter/auth_filter.cpp` 是死代码（未被任何控制器注册），但本次会同步修改以防后续启用。
- JWT 签发/校验在 `backend/src/core/jwt_util.cpp`：当前 payload 只有 `sub/iat/exp` + 业务 extra，**无 `jti`**；过期由 `access_token_expire_minutes`（1440）决定。
- WS 现状：`WsHub`（`backend/src/ws/ws_hub.cpp`）是单例、纯内存，维护 `clientId→ws` 与 `topic→clientId集合`；`broadcast(topic, payload)` 直接遍历本地连接发送。生产者 `sample_pump`（采集器）调用 `hub_.broadcast("data"|"alarm", payload)`。前端 `frontend/src/ws/IbmcpWsClient.ts` 靠业务帧的 `type` 字段推导 topic（`data`/`device_status`→device/`alarm`/`control_finished`→control），后端直接透传业务帧 JSON——因此**只要后端发出的 JSON 字节不变，前端无需改动**。
- 现有控制器端点地图（已枚举）：config（equipments/devices/routes/functions/points 均有 list + CRUD）、static_dict（5 读 + create/update/remove）、runtime_device（list/overview/get/points/point/control/lastValues）、data（history/latest）、alarm（list/stats/get/ack/recover）。

## Functional Requirements

### 数据缓存
- **FR-C1**: 提供统一缓存访问层 `ibmcp::cache`，封装对 Redis 的 get/set/del（JSON 值），key 统一前缀 `cache:`。
- **FR-C2**: 缓存以下读接口的 `data` 响应体（命中则跳过数据库查询）：
  - static_dict：`/api/v1/static/bootstrap`、`/api/v1/static/{dict}/all`、`/api/v1/static/{dict}/code/{code}`、`/api/v1/static/{dict}/{id}`、`/api/v1/static/{dict}`（page）。
  - config 列表：`/api/v1/config/devices`、`/api/v1/config/routes`、`/api/v1/config/routes/{id}/expand`、`/api/v1/config/functions`、`/api/v1/config/points`。
  - 实时数据：`/api/v1/data/latest`。
  - 运行时：`/api/v1/runtime/devices`、`/api/v1/runtime/devices/overview`。
- **FR-C3**: 写时失效：对应资源的 create/update/remove 成功后删除其缓存 key：
  - static_dict 写 → 删除 `cache:static_dict:bootstrap` 与 `cache:static_dict:{dict}:all`；page/byId/byCode 靠 TTL 自然过期。
  - config devices/routes/functions/points 写 → 删除对应列表 key；route 写还删除该 id 的 expand key。
  - `/api/v1/control/write` 成功 → 删除 runtime devices list + overview key。
- **FR-C4**: TTL：static_dict 600s；config 列表 300s；data/latest 5s；runtime 30s。
- **FR-C5**: 缓存 key 必须包含影响输出的全部查询参数（如 data/latest 的 device_code、point_ids；runtime overview 无参数），避免错命中。
- **FR-C6**: Redis 读缓存失败/未命中时回源数据库并回填；Redis 写失败不影响请求返回（只记日志）。
- **FR-C7**: 缓存仅在鉴权通过后使用；这些接口返回数据与用户无关（无 per-user 过滤），不产生越权泄露。

### 实时推送（Pub/Sub + WS）
- **FR-P1**: `WsHub` 持有一个 `RedisSubscriber`，在应用启动后订阅频道模式 `ibmcp:ws:*`；收到消息时按 channel 解析出 topic，将 message（业务帧 JSON）投递给本实例订阅该 topic 的 WS 连接。
- **FR-P2**: `WsHub::broadcast(topic, payload)` 改为：序列化 payload 为 JSON，执行 `PUBLISH ibmcp:ws:<topic> <json>`（`execCommandAsync` 回调，fire-and-forget，保持同步签名，**调用方 sample_pump 零改动**）。
- **FR-P3**: 本实例发布的消息也经订阅回调投递给本地连接（无重复投递）；多实例下任一实例发布，所有实例的本地连接均收到。
- **FR-P4**: 前端收到的 WS 消息内容（JSON 字节）与改造前完全一致——仅传输通道从"进程内直推"变为"Redis Pub/Sub"。
- **FR-P5**: Redis 不可用时：publish 失败仅记日志不抛异常；订阅断连由 Drogon/hiredis 自动重连，重连失败不影响 HTTP 服务。

### Token 黑名单 / 会话
- **FR-T1**: `jwt::createToken` 在 payload 中增加 `jti`（UUID，每个 token 唯一），access 与 refresh token 均携带。
- **FR-T2**: 提供 `ibmcp::auth` 黑名单能力：`isBlacklisted(jti)`（异步回调版，供非协程鉴权回调使用）与 `blacklist(jti, ttlSec)`（协程版，供登出使用）。Redis key 为 `auth:blacklist:<jti>`，值为 `1`，TTL = token 剩余有效期。
- **FR-T3**: 全局鉴权（main.cpp post-routing advice）在 `verifyToken` 通过后，若 payload 含 `jti` 则异步查黑名单；命中则返回 `401 token revoked`。**Redis 故障或 jti 缺失时 fail-open**（放行并记日志），避免鉴权服务被 Redis 拖垮。
- **FR-T4**: 新增 `POST /api/v1/auth/logout`：读取 Bearer token，校验后取 `jti` 与 `exp`，以 `max(0, exp-now)` 为 TTL 写入黑名单；若请求体带 `refresh_token`，同样校验并拉黑其 jti。返回 `code:0`。
- **FR-T5**: `auth_filter.cpp`（虽当前未启用）同步加入黑名单检查，保持一致。
- **FR-T6**: 黑名单 key 随 token 自然过期而到期（TTL 对齐），Redis 自清理，无无限增长。

## Non-Functional Requirements
- **NFR-1 兼容性**: 前端零改动；现有 HTTP 接口响应体结构不变（仅更快）。
- **NFR-2 健壮性**: Redis 故障下三能力均降级不崩溃（缓存回源 / 推送丢弃或本地 / 鉴权 fail-open）。
- **NFR-3 性能**: 黑名单为每次鉴权增加 1 次 Redis 往返（亚毫秒级，可接受）；缓存命中显著减少 PG 查询。
- **NFR-4 可观测**: 关键路径写 INFO/ERROR 日志（缓存命中、黑名单命中、publish 失败、订阅状态）。
- **NFR-5 单实例兼容**: 单实例部署下行为与改造前等价（推送不丢、不重复）。

## Constraints
- **Technical**: Drogon 1.9.5（已重编启用 Redis）；C++20 协程；Redis 7（docker，127.0.0.1:6379 无密码）；构建必须在欧拉端通过（`build.sh` 0 error）。
- **Business**: 不破坏现有 API 契约；不引入前端改动。
- **Dependencies**: `ibmcp::redis::client()` 访问器（已存在）；`jwt::verifyToken/createToken`（已存在）；`WsHub`（已存在）。

## Assumptions
- 这些缓存接口的返回数据无用户级过滤，缓存共享安全（FR-C7 前提）。
- data/latest 5s 缓存新鲜度可接受（实时性要求不高的大屏场景）。
- 部署后重启服务，存量无 jti 的旧 token 仍有效至自然过期（fail-open）；新签发 token 才具备可拉黑能力——可接受，不强制全员重新登录。
- 登出由前端在用户点击退出时调用 `/api/v1/auth/logout`（前端是否接入调用不在本 spec 范围，但接口须可用）。

## Acceptance Criteria

### AC-C1: 缓存命中减少数据库查询
- **Type**: `rule`
- **Given**: 某缓存读接口（如 `/api/v1/config/devices`）首次请求已回填缓存
- **When**: 相同参数再次请求该接口
- **Then**: 命中 Redis 缓存返回，且在缓存有效期内对 PG 的该查询不重复执行（可通过日志/或对比响应一致证明命中）
- **Pass Condition**: 连续两次相同请求返回体一致，且第二次命中缓存（有缓存命中日志或 Redis key 存在）
- **Evidence**: curl 两次接口 + `redis-cli GET cache:config:devices` 存在 + 响应 diff 为空

### AC-C2: 写时失效消除脏读
- **Type**: `rule`
- **Given**: `/api/v1/config/devices` 列表已缓存
- **When**: 调用 `POST/PUT/DELETE /api/v1/config/devices...` 成功
- **Then**: 对应缓存 key 被删除，下一次列表请求回源拿到最新数据
- **Pass Condition**: 写后 `redis-cli EXISTS cache:config:devices` = 0，再次 GET 列表返回新数据
- **Evidence**: 写操作 curl + redis EXISTS + 再次 GET 对比

### AC-C3: TTL 过期自动回源
- **Type**: `rule`
- **Given**: data/latest 缓存 TTL 5s 已写入
- **When**: 等待 >5s 后再次请求
- **Then**: 缓存失效，回源数据库并回填新值
- **Pass Condition**: `redis-cli TTL cache:data:latest:...` 到期后为 -2，再次请求后 key 重建
- **Evidence**: TTL 查询 + 等待后请求

### AC-C4: Redis 故障时缓存降级回源
- **Type**: `rule`
- **Given**: Redis 临时不可用（模拟：停掉 redis 容器）
- **When**: 请求任一缓存读接口
- **Then**: 接口仍返回 200 与正确数据（回源 PG），不抛 500
- **Pass Condition**: 停 redis 后接口 `code:0` 且数据正确；恢复 redis 后缓存重新生效
- **Evidence**: 停容器→curl→恢复→curl

### AC-P1: 推送经 Redis Pub/Sub 且前端消息不变
- **Type**: `rule`
- **Given**: 前端 WS 已连接并订阅 `data` topic，后端有数据推送触发
- **When**: 采集器产生一条数据并 `broadcast("data", payload)`
- **Then**: Redis 频道 `ibmcp:ws:data` 收到 PUBLISH，前端 WS 收到与改造前格式相同的业务帧 JSON
- **Pass Condition**: `redis-cli PSUBSCRIBE ibmcp:ws:*` 能看到消息；前端 onmessage 收到的 JSON 含正确 `type` 字段且能正常 dispatch
- **Evidence**: redis-cli 订阅观测 + 前端/或 WS 客户端抓包消息内容

### AC-P2: 单实例无重复投递
- **Type**: `rule`
- **Given**: 单实例运行，1 个 WS 客户端订阅 `alarm`
- **When**: 触发 1 次 alarm broadcast
- **Then**: 该客户端恰好收到 1 条消息（不因 publish+本地订阅而重复）
- **Pass Condition**: 客户端计数 = broadcast 次数
- **Evidence**: WS 测试客户端统计收到消息数

### AC-P3: 推送通道与 HTTP 解耦（Redis 故障不崩）
- **Type**: `rule`
- **Given**: Redis 不可用
- **When**: 触发 broadcast 与普通 HTTP 请求
- **Then**: broadcast 不抛异常（仅记日志），HTTP 服务正常响应
- **Pass Condition**: 停 redis 后 broadcast 调用无崩溃，`/api/v1/ping` 仍 200
- **Evidence**: 停容器→触发推送→curl ping

### AC-T1: 登出后 token 立即失效
- **Type**: `rule`
- **Given**: 用户登录获得 access token T
- **When**: 用 T 调用 `POST /api/v1/auth/logout`，随后用 T 访问任意受保护接口
- **Then**: 登出返回 code:0；后续请求返回 401 `token revoked`
- **Pass Condition**: logout 后 `redis-cli EXISTS auth:blacklist:<jti>` = 1，受保护接口 401
- **Evidence**: login→logout→curl 受保护接口→redis EXISTS

### AC-T2: 黑名单 TTL 对齐 token 过期自清理
- **Type**: `rule`
- **Given**: 登出写入黑名单 key，TTL = 剩余有效期
- **When**: token 自然过期时间到达
- **Then**: 黑名单 key 自动消失（TTL 到期）
- **Pass Condition**: `redis-cli TTL auth:blacklist:<jti>` ≤ token 剩余秒数，且 > 0
- **Evidence**: TTL 查询

### AC-T3: 未登出的 token 不受影响
- **Type**: `rule`
- **Given**: 用户 A 登出（token A 拉黑），用户 B 的 token B 正常
- **When**: B 用其 token 访问受保护接口
- **Then**: B 正常通过（200），A 的 token 仍 401
- **Pass Condition**: 黑名单按 jti 精准命中，不误伤
- **Evidence**: A 登出后 B 仍 200

### AC-T4: Redis 故障时鉴权 fail-open
- **Type**: `rule`
- **Given**: Redis 不可用，用户持有有效 token
- **When**: 访问受保护接口
- **Then**: 放行（不因黑名单查询失败而拒绝），日志有告警
- **Pass Condition**: 停 redis 后有效 token 仍 200
- **Evidence**: 停容器→curl 受保护接口

### AC-X1: 全量回归不破坏现有接口
- **Type**: `rule`
- **Given**: 部署新二进制后
- **When**: 跑通登录（需先启用账号）+ 核心读接口 + WS 连接
- **Then**: 现有接口行为与响应结构不变，无新增 5xx
- **Pass Condition**: 核心接口回归通过，无 5xx
- **Evidence**: 回归 curl 脚本输出

## Open Questions
- [ ] data/latest 缓存 TTL 取 5s 是否满足业务实时性要求？（默认 5s，可配）
- [ ] 登出是否需要前端同步调用 `/api/v1/auth/logout`？（本 spec 只保证后端接口可用）
- [ ] 是否要把告警列表 `/api/v1/alarms/list` 也纳入缓存（TTL 5s，ack/recover 失效）？当前 scope 暂不含，可后续加。
