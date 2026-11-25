# IBMCP Drogon 版本文档

本目录为 IBMCP「纯 C++ Drogon 框架」重构方案的完整文档集。

## 核心变更

| 维度 | 旧方案（FastAPI + C++ pybind11） | 新方案（纯 C++ Drogon） |
|------|----------------------------------|-------------------------|
| 后端语言 | Python 3.11（FastAPI / asyncpg / Pydantic） | C++17/20（Drogon） |
| 采集实现 | C++ 编译为 `_core.so`，经 pybind11 被 Python 调用 | C++ 采集代码作为静态库直接链入 Drogon 进程 |
| 跨语言边界 | 有（Python ↔ C++，需 stub/native 双实现） | 无（单一 C++ 进程） |
| HTTP/WS | FastAPI / Starlette WebSocket | Drogon `HttpController` / `WebSocketController` |
| 数据库访问 | asyncpg（异步连接池） | Drogon `DbClient`（libpq，异步 + C++20 协程） |
| ORM / 校验 | SQLAlchemy 迁移 + Pydantic | Drogon Model（`drogon_ctl create model`）+ nlohmann/json |
| 鉴权 | PyJWT + argon2-cffi | jwt-cpp（header-only）+ libargon2 |
| 缓存/会话/推送 | redis-py（预留未用） | Drogon `RedisClient`（hiredis）：响应缓存 + Token 登出黑名单 + WS Pub/Sub 推送骨干（见 [09](./09-Redis缓存与实时推送.md)） |
| 迁移工具 | Alembic | 原生 SQL 脚本 + 版本表迁移器 |
| 测试 | pytest | GoogleTest（与 backend/collector 现有测试统一） |
| 构建 | pybind11 CMake + uv/venv | 纯 CMake（`find_package(Drogon)`） |
| 运行 | uvicorn + 虚拟环境 | 单二进制 `ibmcp_server` 可执行文件 + systemd |
| Windows 开发 | 需 stub 后端（无 Linux .so） | 编辑在 Windows 11（CLion/VSCode），构建与运行在欧拉；`eSync` 一键同步源码 |

> 部署环境：openEuler 24.03 LTS 虚拟机（`192.168.80.136`，用户 `meng`）。开发在 Windows 11，构建/运行在欧拉。所有 C++ 库与工具统一部署在用户可写目录 **`/home/meng/ibmbuild`**（Drogon/Trantor 静态库、libmodbus/open62541 动态库、`env.sh`、ldconfig 注册），不污染 `/usr/local`。Docker 部署 PG15 / Redis7 / Node-RED，Nginx 托管前端并反代，systemd 拉起单一二进制 `ibmcp_server`（运行时工作目录 `dist/`）。详见 [05-openEuler 构建与部署](./05-openEuler构建与部署.md)。

## 文档清单

| 编号 | 文档 | 说明 |
|------|------|------|
| 01 | [01-Drogon 架构设计](./01-Drogon架构设计.md) | 整体架构、组件映射、进程/线程模型、设计决策 |
| 02 | [02-数据模型与 ORM 映射](./02-数据模型与ORM映射.md) | 数据库表 → Drogon Model，DbClient 配置，迁移策略 |
| 03 | [03-接口契约与 Controller 映射](./03-接口契约与Controller映射.md) | 78 个 REST 端点 + WebSocket → Drogon 控制器，统一包膜与鉴权 Filter |
| 04 | [04-采集器融合与调度](./04-采集器融合与调度.md) | backend/collector 静态库融合、事件回调链路、SamplePump C++ 化 |
| 05 | [05-openEuler 构建与部署](./05-openEuler构建与部署.md) | Windows11 开发→欧拉部署工作流、`/home/meng/ibmbuild` 布局、Drogon（静态）+ 驱动库、`env.sh`/ldconfig、CMake 构建到 `dist/`、Docker、Nginx、systemd、部署脚本 |
| 06 | [06-迁移清单与差异对照](./06-迁移清单与差异对照.md) | 文件删除清单、模块迁移对照表、风险与注意事项 |
| 07 | [07-跨机器迁移与离线交付方案](./07-跨机器迁移与离线交付方案.md) | 新机器交付、离线 bundle 还原、PG / Redis / Node-RED 数据迁移 |
| 08 | [08-CLion 远程开发配置](./08-CLion远程开发配置.md) | CLion 远程工具链 / gdbserver 调试配置 |
| 09 | [09-Redis 缓存与实时推送](./09-Redis缓存与实时推送.md) | Redis 三项落地能力：响应缓存（Cache-Aside）、Token 黑名单（登出即失效）、WS Pub/Sub 推送骨干，含 fail-open 与运维观测 |

## 阅读顺序建议

1. 先读 `01` 建立整体认知；
2. 实施时按 `02 → 03 → 04` 顺序编码；
3. 联调与上线参考 `05`；
4. `06` 作为迁移过程的核对清单；
5. 跨机器交付 / 离线还原见 `07`，CLion 远程调试见 `08`；
6. Redis 缓存 / 黑名单 / 推送的实现与运维见 `09`。