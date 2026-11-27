# IBMCP 黑盒管控系统（后端）

基于 [Drogon](https://github.com/drogonframework/drogon) 框架开发的工业设备黑盒管控后端服务。系统将 Modbus / MQTT / OPC UA 等异构协议的现场设备统一建模，提供设备配置、数据采集、实时监控、远程控制、告警与审计的闭环管控能力，可作为工业数据采集与设备管控平台的后端核心。

## 系统定位

IBMCP（Industrial Black-box Control Platform）面向工业现场"黑盒设备"的统一接入与管控：

- **统一接入**：屏蔽不同厂商、不同协议设备的差异，以一致的"设备—功能—点位"模型对外暴露能力。
- **配置驱动采集**：采集计划由配置编译生成，支持热加载与版本化管理，无需重启即可调整采集任务。
- **闭环管控**：覆盖"建模 → 采集 → 监控 → 控制 → 告警 → 审计"全链路，控制命令带下发确认机制。
- **实时联动**：采集数据通过 WebSocket 实时推送前端，并可对接 Node-RED 做流程编排。

## 核心功能

### 1. 设备建模与配置管理
- **静态字典**：维护设备类别、品牌型号、端口、协议等基础字典（`/api/v1/static/*`），前端可一次性 `bootstrap` 拉取全量字典。
- **设备档案**：设备基本信息增删改查，绑定类别、品牌型号、端口与支持协议。
- **功能与点位**：为设备定义功能（function）和采集/控制点位（point），点位包含地址、数据类型、读写属性等元数据。
- **采集路由**：配置采集路由并支持展开（expand），将逻辑路由解析为实际点位集合。
- **装备与采集计划**：将设备/点位编排为可下发的采集计划，提供编译、激活、卸载、同步、清理等生命周期操作，并记录计划修订版本（revisions）与变更审计。

### 2. 多协议数据采集
- **协议驱动**：内置 Modbus TCP / Modbus RTU（libmodbus）、MQTT（libmosquitto）、OPC UA（open62541）驱动，通过驱动工厂按协议动态加载；未安装对应协议库时相关驱动自动编译关闭。
- **计划调度**：采集计划解析与校验后，由时间轮（timewheel）按周期触发，IO 线程池并发执行；支持模拟量（sim_value）注入用于调试。
- **采集器运维**：提供健康检查、支持协议列表、运行时统计与热重载（`/api/v1/collector/*`）。

### 3. 实时监控与推送
- **WebSocket 推送**：`/ws/push` 向前端推送采集数据与事件；`/api/v1/ws/collect` 作为采集侧数据接入通道。
- **采样泵**：按固定间隔（`sample_pump_interval_ms`）批量（`sample_pump_batch_size`）抽取最新采集值推送前端。
- **运行时视图**：运行时设备列表、总览（overview）、单设备详情、点位列表与当前值、最近值快照。

### 4. 远程控制
- **点位写入**：向指定设备点位下发写值命令，按点位数据类型校验取值，可配置是否等待结果与超时。
- **命令追踪**：每条命令生成 `command_id`，可查询执行结果与控制队列深度。
- **下发确认闭环**：通过 `command_waiter` 机制，命令下发后同步等待设备回写确认，确保控制动作可靠到达。

### 5. 告警管理
- 告警列表、统计、详情查询；支持告警确认（ack）与恢复（recover）处理。

### 6. 数据查询
- **历史数据**：按点位与时间范围查询，支持分桶（bucket）聚合（func）与条数限制。
- **最新值**：按设备批量查询点位当前值。

### 7. 用户与权限
- 登录 / 登出 / 令牌刷新 / 令牌校验。
- 用户管理：增删改查、启用禁用、修改密码、分配角色。
- 角色管理：角色增删改查。
- 基于 JWT 的 Bearer 鉴权，结合 Redis 黑名单实现令牌即时吊销。

### 8. 审计
- 配置变更审计：记录每次配置操作的目标层级、目标对象、动作、操作人、计划变更前后版本及详情（`audit_config_change`）。

### 9. Node-RED 集成
- 管控 Node-RED 编辑器容器（`ibmcp-nodered`）的状态查询与启动 / 停止 / 重启，便于与采集数据联动实现可视化流程编排。

### 10. 系统运维
- 健康检查：`GET /api/v1/ping`（应用存活）、`GET /api/v1/redis/ping`（Redis 连通性）。
- 开放 API 文档：`/api/v1/docs`、`/api/v1/redoc`、`/api/v1/openapi.json`。

## 业务数据流

- **采集链路**：现场设备 → 协议驱动 → 采集器调度执行 → 数据存储 / 采样泵 → WebSocket → 前端。
- **控制链路**：前端 → HTTP API（`/api/v1/control/write`）→ `ControlService` → 采集器写点位 → 设备执行 → 回写确认 → `command_waiter` 返回结果。
- **配置链路**：前端配置变更 → 配置 API → 编译生成采集计划 → 激活下发到采集器 → 触发审计记录。

## 技术栈

| 类别 | 技术 |
| --- | --- |
| 语言 | C++20 |
| Web 框架 | Drogon（含 Trantor 网络库） |
| 关系数据库 | PostgreSQL（libpq） |
| 缓存 / 会话 | Redis（hiredis） |
| 工业协议 | Modbus TCP/RTU（libmodbus）、MQTT（libmosquitto）、OPC UA（open62541） |
| 鉴权 | JWT + Redis 黑名单；密码哈希可选 Argon2 |
| 构建 | CMake ≥ 3.20 |
| 目标平台 | openEuler（远程工具链构建） |

## 目录结构

```
.
├── CMakeLists.txt              # 顶层 CMake，仅引入 backend
└── backend/
    ├── CMakeLists.txt          # 后端主构建脚本
    ├── config.json             # 运行时配置（监听端口、DB、Redis、自定义参数）
    ├── migrations/             # 数据库迁移 SQL（按编号顺序执行）
    ├── include/                # 后端头文件（controller / core / db / service / ws 等）
    ├── src/                    # 后端源码
    │   ├── main.cpp            # 服务入口：加载配置、迁移、CORS、鉴权、启动采集器
    │   ├── collector/          # 采集器桥接：编排器、事件分发、采样泵、计划编译
    │   ├── controller/         # HTTP 控制器（REST API）
    │   ├── core/               # 应用上下文、配置、鉴权、JWT、缓存、错误码
    │   ├── db/                 # 数据库连接、迁移器、Redis 封装
    │   ├── filter/             # 鉴权过滤器、CORS 过滤器
    │   ├── service/            # 业务服务（控制、查询、审计、静态字典）
    │   └── ws/                 # WebSocket 推送中心
    └── collector/              # 采集器核心（静态库 ibmcp_collector）
        ├── include/collector/  # 对外接口 collector.h
        ├── src/
        │   ├── drivers/        # 协议驱动：modbus_tcp / modbus_rtu / mqtt / opcua + 工厂
        │   ├── scheduler/      # 时间轮、IO 线程池、采集循环、模拟量
        │   ├── plan/           # 采集计划解析与校验
        │   ├── store/          # 数据存储
        │   └── common/         # 事件发射器、日志
        └── tests/              # 采集器单元测试（GTest，EXCLUDE_FROM_ALL）
```

## 构建产物

- `ibmcp_server` — 主服务可执行文件（HTTP + WebSocket）
- `ibmcp_collector` — 采集器静态库，链接进 `ibmcp_server`
- `collector_tests` — 采集器单元测试（需显式构建，默认不进入 all 目标）

## 依赖准备

依赖库统一安装到 `IBMCP_BUILD_DIR`（默认 `/home/meng/ibmbuild`），`backend/CMakeLists.txt` 会在该路径下查找 Drogon / Trantor，并用 `pkg-config` 探测协议库。

必需依赖：

- Drogon（`BUILD_REDIS=ON`，内含 hiredis 传递依赖）
- OpenSSL、ZLIB
- PostgreSQL 开发库（`libpq` / `postgresql-devel`）
- hiredis 开发库（`hiredis-devel`）

可选依赖（缺失时对应协议驱动不可用，编译宏自动关闭）：

- libmodbus → `IBMCP_HAS_MODBUS`
- libmosquitto → `IBMCP_HAS_MQTT`
- open62541 → `IBMCP_HAS_OPCUA`
- argon2 → `IBMCP_HAS_ARGON2`（密码哈希）

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

单元测试（可选）：

```bash
cmake --build build --target collector_tests
ctest --test-dir build/backend/collector/tests --output-on-failure
```

## 配置

运行时通过 `backend/config.json` 配置，主要项：

- `listeners` — HTTP 监听地址与端口（默认 `0.0.0.0:8000`）
- `db_clients` — PostgreSQL 连接（默认库 `ibmcp`，用户 `ibmcp`）
- `redis_clients` — Redis 连接（默认 `127.0.0.1:6379`）
- `custom_config`
  - `app_secret_key` — JWT 签名密钥（生产环境务必修改）
  - `access_token_expire_minutes` — 访问令牌有效期（默认 1440 分钟）
  - `cors_origins` — 允许的跨域来源列表
  - `pg_dsn` — 迁移用 DSN
  - `migrations_dir` — 迁移文件目录
  - `sample_pump_interval_ms` / `sample_pump_batch_size` — 采样泵节奏
  - `public_paths` — 免鉴权路径白名单

## 运行

```bash
cd backend
./ibmcp_server
```

启动流程：

1. 加载 `config.json`
2. 执行 `migrations/` 下的数据库迁移
3. 注册 CORS 预检（OPTIONS）与全局 Bearer 鉴权
4. 初始化应用上下文与 WebSocket 推送中心
5. 启动采集器编排器（bootstrap）
6. 开始监听 HTTP / WebSocket

默认监听 `http://0.0.0.0:8000`，OpenAPI 文档位于 `/api/v1/docs` 与 `/api/v1/redoc`，健康检查 `GET /api/v1/ping`、`GET /api/v1/redis/ping`。

## 数据库迁移

迁移文件位于 `backend/migrations/`，按文件名数字前缀顺序执行，服务启动时由 `Migrator` 自动应用。

| 文件 | 说明 |
| --- | --- |
| 0001_initial_schema.sql | 初始 schema：静态字典、设备基础信息等 |
| 0002_rbac_sys_users.sql | 用户与 RBAC 相关表 |
| 0003_control_ext_command_id.sql | 控制命令扩展字段 |
| 0004_drop_legacy_rbac.sql | 清理遗留 RBAC 表 |
| 0005_device_supported_ports_items.sql | 设备支持端口表 |

## 说明

- 仓库中 `cmake-build-debug-oe-remote/` 为本地镜像的 openEuler 远程构建产物，不纳入版本管理。
- 前端为独立工程（默认 Vite 开发地址 `http://localhost:5173`），通过 `cors_origins` 放行。