# 01 - Drogon 架构设计

## 1. 设计目标

将现有「FastAPI（Python 业务层）+ C++ pybind11 采集扩展」的双语言融合架构，重构为**单一 C++ 进程、基于 Drogon 框架**的纯 C++ 架构，达成：

1. **移除 Python 技术栈**：不再依赖 FastAPI / uvicorn / asyncpg / Pydantic / pybind11 / venv / uv；
2. **消除跨语言边界**：`backend/collector` 不再编译为 `_core.so`，而是作为静态库直接链入 Drogon 可执行文件，采集与业务同进程、同线程模型；
3. **保留全部业务能力**：五级配置链编译、计划下发编排、200ms 采样泵、批量 COPY 入库、报警判定、设备控制（wait 同步等待）、WebSocket 四主题推送、审计日志、JWT 鉴权等；
4. **保持部署拓扑不变**：openEuler 24.03 + Docker（PG15/Redis7/Node-RED）+ Nginx（前端托管 + 反向代理）+ systemd；
5. **以欧拉工具链为准**：构建、测试均在 openEuler 上用 GCC + CMake 完成。

## 2. 架构总览

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                        浏览器  /  Tauri（桌面端）                              │
│                  Vue3 前端（构建产物由 Nginx 托管）                            │
└───────┬───────────────────────────────────────┬──────────────────────────────┘
        │ HTTP /api/v1/*   WS /ws/push          │ 直连 127.0.0.1:8000（Tauri 同机）
        ▼                                        ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                        Nginx（openEuler 裸机）                                 │
│          静态托管 frontend-dist  +  反向代理 127.0.0.1:8000（HTTP + WS）      │
└────────────────────────────────────┬─────────────────────────────────────────┘
                                     ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                  ibmcp 进程（Drogon，单一 C++ 可执行文件）                     │
│                                                                              │
│  ┌──────────────── HttpControllers ───────────────┐  ┌──── WsController ────┐ │
│  │ auth/static/config/data/runtime/alarms/        │  │ /ws/push             │ │
│  │ control/collector/ping/node-red                │  │ 四主题订阅广播        │ │
│  └────────────────────────┬───────────────────────┘  └──────────┬───────────┘ │
│                           │ 统一包膜 Advice / JWT 鉴权 PostRoutingAdvice │     │
│  ┌────────────────────────▼─────────────────────────────────────▼──────────┐  │
│  │                          业务 Service 层（C++）                           │  │
│  │  AuthService │ PlanCompiler │ CollectorOrchestrator │ QueryService       │  │
│  │  ControlService │ AuditService │ ...                                      │  │
│  └──────┬───────────────────────────────────────────────────────┬───────────┘  │
│         │ UI 查询（读）                                         │ 计划下发/控制  │
│         ▼                                                       ▼             │
│  ┌─────────────────┐                                  ┌──────────────────┐    │
│  │ DbClient default │                                  │ collector 静态库  │    │
│  │ （读为主，UI 查询）│                                  │ 驱动 / 调度 / 存储 │    │
│  └────────┬────────┘                                  └────────┬─────────┘    │
│           │                                                    │ 事件回调 / 轮询 │
│           │              ┌─────────────────────────────────────┘               │
│           │              ▼                                  ▼                  │
│           │  ┌─────────────────────────┐   ┌─────────────────────────────┐   │
│           │  │ 通路A：事件回调           │   │ 通路B：采样泵（200ms）       │   │
│           │  │ EventDispatcher         │   │ SamplePump                  │   │
│           │  │ （device_key 合并去抖）  │   │ ├─ ActiveAlarmTracker       │   │
│           │  │ ├─ device_status 写库   │   │ ├─ 专用 libpq 连接 → COPY    │   │
│           │  │ ├─ plan_ack 写库        │   │ └─ 告警写库                  │   │
│           │  │ └─ write_result →       │   │                              │   │
│           │  │   CommandWaiter         │   │                              │   │
│           │  └───────────────┬─────────┘   └──────────────┬──────────────┘   │
│           │                  │ 采集侧写入                  │ WS 消息投递        │
│           │                  └──────────────┬─────────────┘                  │
│           │                                 ▼                                │
│           │                       ┌──────────────────┐                       │
│           │                       │ DbClient write   │                       │
│           │                       │ （采集写入专用）   │                       │
│           │                       └──────────────────┘                       │
│           │                                                                    │
│  ┌────────────────────────────────────────────────────────────────────────┐  │
│  │ WsHub：WsController 订阅表；SamplePump / EventDispatcher 只投递到待发队列，│  │
│  │ 实际 send 在 Drogon IO 线程异步完成（热路径绝不阻塞，见 §4.3）           │  │
│  └────────────────────────────────────────────────────────────────────────┘  │
└───────────┬──────────────────────────────────────────────────────────────────┘
            ▼
   ┌───────────────────────────────────────┬──────────────────────────────────┐
   │  PostgreSQL 15（Docker 容器）          │  Redis 7（Docker 容器）          │
   │  配置 / 时序（COPY 专用连接）           │  ① 响应缓存（Cache-Aside）       │
   │  / 报警 / 审计                         │  ② Token 黑名单（登出即失效）    │
   │                                       │  ③ WS Pub/Sub 推送骨干（见 §6.6）│
   └───────────────────────────────────────┴──────────────────────────────────┘
            ▲
   ┌──────────────────────────────────────────────────────────────────────────┐
   │  Node-RED（Docker 容器） — PLC 模拟器 / 协议探针，仅联调使用               │
   └──────────────────────────────────────────────────────────────────────────┘
```

> 读图要点：
> - **两条数据通路**：通路 A 是 collector 事件回调（低频、状态/命令类），通路 B 是 200ms 采样泵（高频、数据类）。报警判定属于通路 B（SamplePump 内的 ActiveAlarmTracker），不在 EventDispatcher 上。
> - **读写连接池分离**：UI 查询走 `default` 池，采集侧写入走 `write` 池，避免采集写风暴拖慢用户面查询。
> - **COPY 专用连接**：`time_series_data` 批量写入走独立 libpq 连接，与 Drogon 两个连接池都隔离，不阻塞池内连接（见 02 §6）。
> - **Redis 已接入核心链路**：承载响应缓存（Cache-Aside）、Token 黑名单（登出即失效）、WS Pub/Sub 推送骨干；进程内通过 `RedisClient` 访问，故障时 fail-open（缓存回源 / 鉴权放行 / 推送仅记日志），不阻塞 HTTP（见 §6.6 与 09）。
> - **热路径不碰 WS send**：采样泵 / 事件分发线程只把消息放进 WsHub 待发队列，真正发送在 Drogon IO 线程完成，慢客户端不拖累 200ms 泵。

## 3. 组件映射（FastAPI → Drogon）

| 旧组件（Python） | 新组件（C++/Drogon） | 说明 |
|------------------|----------------------|------|
| `FastAPI()` / `create_app()` | `drogon::app()` | 应用入口，注册控制器/过滤器/插件 |
| `lifespan` 启动/关闭钩子 | `app().registerController(...)` + 启动前初始化 + 信号/插件 | Service 单例在 `main` 中显式 init/shutdown |
| `APIRouter` + `@router.xxx` | `HttpController`（`METHOD_ADD`/`METHOD_LIST` 宏） | 每个 Python 路由文件 → 一个 Controller 类 |
| `@router.websocket` | `WebSocketController<>` | `/ws/push` 单端点，按 `topic` 字段分发 |
| Pydantic `BaseModel`（请求/响应） | nlohmann/json + 手写 DTO struct 或 Drogon 代码生成 Model | 请求体解析与校验 |
| `asyncpg` 连接池 | `app().getDbClient()`（libpq 异步池，`config.json` 配置） | 支持 C++20 协程 `co_await` |
| SQLAlchemy/Alembic 迁移 | SQL 脚本 + `migration` 版本表 + 启动期迁移器 | 见 `02` |
| `Depends(get_current_user)` | 全局 `registerPostRoutingAdvice`（`main.cpp`） | 解析 `Authorization: Bearer`，校验后写入 `req->attributes()` 的 `user`；`AuthFilter` 类保留但不挂路由（Drogon 1.9.5 不允许手动注册 `HttpFilter` 子类） |
| PyJWT | jwt-cpp（header-only，`jwt-cpp/jwt.h`） | 签发/校验 HS256 |
| argon2-cffi | libargon2（`argon2.h`） | 密码哈希 |
| redis-py | Drogon `RedisClient`（封装 hiredis） | 响应缓存 / Token 登出黑名单 / WS Pub/Sub 推送骨干（详见 09） |
| structlog | spdlog（Drogon 自带，或独立引入） | 结构化日志 |
| `ResultEnvelopeMiddleware` | Drogon 后置 Advice / 响应拦截器 | 统一 `{code, message, data}` 包膜，HTTP 恒 200 |
| `CORSMiddleware` | `app().enableCORS(...)` 或自定义 Filter | 开发期放开 |
| 异常处理器 | 全局异常 → 统一包膜 | 业务异常抛自定义异常，Advice 捕获转 JSON |

## 4. 进程与线程模型

### 4.1 单一进程

整个后端是一个可执行文件 `ibmcp_server`（`backend/CMakeLists.txt:56`，systemd 拉起）。无 Python 解释器、无 GIL、无 IPC。

### 4.2 线程构成

| 线程组 | 来源 | 职责 |
|--------|------|------|
| Drogon IO 线程 | `app().setThreadNum(N)`（默认 CPU 核数） | HTTP 请求解析、Controller 调用、WebSocket 收发 |
| DbClient 线程 | Drogon 内部（每连接一个回调线程） | libpq 异步结果回调；声明 `default`（读，UI 查询）与 `write`（写，采集侧）两个池隔离读写负载，COPY 走独立 libpq 连接不入池 |
| 采集器工作线程 | collector 静态库：IO 线程池（按设备分组） + 时间轮调度线程 | 协议读写、周期触发 |
| SamplePump 线程 | 独立线程（200ms 轮询） | `snapshot_since` 拉取 → 批量 COPY 入库 → 报警判定 → 事件分发 |
| EventDispatcher 线程 | 独立消费线程 | 消费 collector 扩展事件，投递到 WsHub / CommandWaiter |
| CommandWaiter | 无独立线程，复用 `std::future`/条件变量 | `wait=true` 控制命令同步等待回执 |

### 4.3 关键并发约束（与旧架构一致）

- **批量入库与查询不互斥**：COPY 走独立事务，与 SELECT 并发；
- **采样泵阻塞不拖垮 HTTP**：泵在自有线程，DB 慢时仅堆积缓冲（无锁快照缓冲），不占用 Drogon IO 线程；
- **WsHub 广播线程安全**：连接表读写加锁，广播串行化到 Drogon IO 线程（`trantor::EventLoop` 的 `runInLoop`）；
- **控制命令 wait 不阻塞 IO**：Controller 协程 `co_await` future，不占用线程。
- **读写连接池分离**：采集侧写入（device_status、报警、控制命令状态）统一走 `write` 池，UI 查询走 `default` 池，防止设备状态抖动引发的写风暴饿死用户面查询；`time_series_data` 的 COPY 走独立 libpq 连接，不占任何池。
- **热路径绝不阻塞 WS 发送**：SamplePump / EventDispatcher 线程只把消息投入 WsHub 待发队列后立即返回，真正的 `send` 在 Drogon IO 线程异步完成；WsHub 对单连接设缓冲上限，慢连接主动踢断，避免拖累 200ms 泵。

## 5. 目录结构（新增 `backend/`）

```
02-IBMCP/
├── backend/                    # Drogon 后端（单一 C++ 工程，无 Python）
│   ├── CMakeLists.txt          # 主工程：enable_testing() + add_executable(ibmcp_server)，链接 Drogon + ibmcp_collector
│   ├── config.json             # Drogon 运行配置（监听、DB、Redis、日志、public_paths 白名单）
│   ├── migrations/             # SQL 迁移脚本（0001~0005，替代 Alembic）
│   ├── include/                # 头文件（无 ibmcp/ 包装层，直接按模块分目录）
│   │   ├── controller/         # 14 个 HttpController + 1 个 WsController 头文件
│   │   ├── core/               # Config, Result, Errors, JwtUtil, PasswordUtil, Cache, AuthRedis
│   │   ├── db/                 # DbClient / Redis 封装、迁移器
│   │   ├── filter/             # AuthFilter, CorsFilter
│   │   ├── service/            # 业务 Service
│   │   ├── ws/                 # WsHub（Redis Pub/Sub 推送骨干）
│   │   ├── collector/          # 采集器对外头文件（CollectorBridge 等）
│   │   ├── control/            # 控制命令相关
│   │   └── util/               # 工具
│   ├── src/                    # 对应实现
│   │   └── main.cpp            # 入口：初始化 Service → app().run()
│   └── collector/              # 采集核心静态库（已移除 pybind11 绑定层）
│       ├── CMakeLists.txt      # add_library(ibmcp_collector STATIC ...) + 条件 add_subdirectory(tests)
│       └── tests/              # GoogleTest 单元测试（EXCLUDE_FROM_ALL，由 test.bat 构建运行）
├── collector-python/           # 删除（见 06）
├── frontend/                   # 不变（Vue3，构建产物给 Nginx）
├── deployments/openeuler/      # 更新：bootstrap.sh / build.sh / deploy.sh（移除 Python）
└── packaging/
    ├── nginx/ibmcp.conf        # 反代 /api→8000、/ws 升级、/nodered/→1880（Node-RED 编辑器）
    └── systemd/ibmcp-api.service  # 改为启动 ibmcp_server 二进制，移除 venv/uvicorn
```

## 6. 关键设计决策

### 6.1 为什么移除 Python

- 旧架构中 Python 仅承担「业务编排 + HTTP 外壳」，真正重活（协议解析、调度、批量入库）在 C++；Python 层是多余的中间层，带来：venv 依赖管理、跨语言序列化开销、Windows 上无法加载 `.so` 需维护 stub 双实现、GIL 导致 `wait=true` 控制命令并发受限。
- 纯 C++ 后：单一二进制部署、零 Python 运行时依赖、控制命令并发不受 GIL 限制、Windows 可直接编译调试（不再需要 stub）。

### 6.2 采集器静态库融合（不做进程拆分）

`backend/collector` 已提供纯 C++ 接口（`ibmcp::Collector`，见 `04`），与 Drogon 链接无技术障碍。保持同进程可：
- 事件回调零拷贝直达业务层；
- `snapshot_since` 返回的 `std::vector<std::map<...>>` 直接在内存中转换为 COPY 批量，无需序列化；
- 控制命令 `write_point` 同步调用，`wait=true` 用 `std::promise` 实现，简单可靠。

不拆为独立采集进程，避免引入 IPC（Unix socket / 共享内存）复杂度。

### 6.3 Drogon DbClient vs 手写 libpq

采用 Drogon 内置 `DbClient`（底层 libpq）：
- 原生异步，与 Drogon IO 线程集成；
- 支持 C++20 协程（`co_await db->execSqlCoro(...)`），Controller 方法可写为 `Task<T>`，近似旧 `async def` 体验；
- 连接池在 `config.json` 声明，与旧 `PG_POOL_MIN/MAX` 对齐。

声明两个具名连接池以隔离读写负载：
- `default`：读为主，服务所有 Controller 的 UI 查询；
- `write`：专给采集侧写入（EventDispatcher 的 device_status / plan_ack / 控制命令状态、SamplePump 的告警写入）。

理由：旧实现查询与采集写入共用同一个 asyncpg 池，设备频繁断连 / 恢复时 device_status 写风暴会拖慢用户面查询。分离后 UI 响应始终稳定。`time_series_data` 的批量 COPY 不走任何池，单独持一个 libpq 连接（见 02 §6），进一步避免阻塞池内连接。

### 6.4 统一响应包膜

旧约定：HTTP 恒 200，业务结果由 `{code, message, data}` 表达（`code=0` 成功）。Drogon 用「后置 Advice」实现：
- Controller 返回 `nlohmann::json`（业务 data）；
- Advice 拦截后包装为 `{"code":0,"message":"ok","data":...}`；
- 异常由全局异常处理器捕获，转 `code=非0` 的包膜，HTTP 仍 200。

### 6.5 配置方式

旧：`.env` + Pydantic Settings。新：采用 **Drogon `config.json` 为主 + 少量环境变量覆盖**：
- DB / Redis / 监听端口 / 线程数 / 日志 → `config.json`（Drogon 原生格式）；
- 业务参数（JWT 密钥、过期时间、CORS、采样间隔）→ 自定义 `Config` 类从 `config.json` 的自定义段或环境变量读取；
- 敏感信息（密码、密钥）支持环境变量覆盖，便于容器化与运维。

### 6.6 Redis 的定位（已启用，承载三项核心能力）

Redis 已从「预留组件」落地为后端核心运行时依赖，承载三项能力（详见 [09](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/docs/0-Drogon/09-Redis缓存与实时推送.md)）：

1. **响应数据缓存**（Cache-Aside）：字典 / 配置 / 概览 / 最新值等读多写少接口，读时回填、写时失效，TTL 5–600s 分级。命中直接返回 Redis 序列化 JSON，未命中回源 PG 并回写。
2. **Token 黑名单 / 会话失效**：每条签发的 JWT 带唯一 `jti`；`POST /api/v1/auth/logout` 将 `jti` 写入 `auth:blacklist:{jti}` 并设 TTL = 剩余有效期；全局 Post-Routing Advice 异步查黑名单，命中即拒（body `code=401`）。
3. **WS 实时推送骨干**（Pub/Sub）：`WsHub` 由纯进程内广播改为 `PUBLISH ibmcp:ws:<topic>` + `psubscribe ibmcp:ws:*`，广播与投递解耦，为多实例水平扩展铺路；`sample_pump` 调用点零改动，前端消息格式兼容。

关键设计：
- **fail-open**：Redis 不可用时不崩、不 500——缓存回源、鉴权放行（仅丧失登出即失效）、推送仅记日志；
- **有界超时**：`config.json` 设 `redis_clients[].timeout = 1.0`（秒），Redis 宕机时命令 ≤1s 失败触发 fail-open，避免请求挂起；
- **`config.json` 必须声明 `default` redis client**，部署侧 Redis 容器由 `docker compose up -d` 拉起（`deploy.sh --full` 含 Redis）。

### 6.7 单进程的崩溃域与边界保护

采集器与业务同进程（§6.2）换取零拷贝，但代价是协议驱动的段错误会带崩整个 HTTP / WS / 控制面（pybind11 时代同样存在此问题）。缓解措施：
- 采集器线程边界必须 catch 所有异常，禁止异常逸出到 Drogon 线程；
- `/collector/health` 暴露各线程（采集器、采样泵、分发线程）存活探针，便于 systemd / 监控感知；
- systemd 单元配置 `Restart=always` 兜底，崩溃后自动拉起；迁移器幂等（见 02 §5），重启不会重复建表。

## 7. 与旧架构的能力对照

| 能力 | 旧实现 | 新实现 | 状态 |
|------|--------|--------|------|
| 五级配置链编译 | `PlanCompiler`（Python） | `PlanCompiler`（C++ 重写） | 迁移 |
| 计划下发 + 版本幂等 | `CollectorOrchestrator` | 同名 C++ 类 | 迁移 |
| 200ms 采样 + COPY 入库 | `SamplePump` / `IngestService` | 同名 C++ 类 | 迁移 |
| 报警判定 | `IngestService._handle_alarm` | `SamplePump` 内 `ActiveAlarmTracker`（04 §3.2） | 迁移 |
| 设备控制 wait 同步 | `ControlService` + `CommandWaiter` | 同名 C++ 类 + `std::promise` | 迁移 |
| WS 四主题推送 | `WsHub`（asyncio） | `WsHub`（C++，Drogon WS） | 迁移 |
| 扩展事件消费 | `EventDispatcher`（asyncio 队列） | 同名 C++ 类（线程安全队列） | 迁移 |
| JWT 鉴权 / 密码 | PyJWT + argon2 | jwt-cpp + libargon2 | 替换 |
| 审计日志 | `AuditService` | 同名 C++ 类 | 迁移 |
| 响应数据缓存 | （无 / 进程内缓存） | `ibmcp::cache`（Redis Cache-Aside，TTL 5–600s，写时失效） | 新增 |
| Token 登出即失效 | （无，JWT 仅靠过期） | jti 黑名单 `auth:blacklist:{jti}`，全局 Advice 异步校验，fail-open | 新增 |
| WS 跨实例推送 | WsHub 进程内广播 | WsHub + Redis Pub/Sub（`ibmcp:ws:*`），广播与投递解耦 | 增强 |
| 字典 / 配置 / 数据 / 报警 / 运行时查询 | 各路由 | 对应 HttpController | 迁移 |
| OpenAPI 文档 | FastAPI 自动 | Drogon 不内置；可选 Swagger 静态文件 | 简化 |