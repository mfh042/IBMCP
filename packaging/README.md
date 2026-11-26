# packaging/ — 运行时打包模板

本目录存放 IBMCP **运行期物料的模板 / 备选方案**，由 `deployments/openeuler/` 下的构建部署脚本消费，或作为容器化的备选入口。

主部署路径（openEuler 裸机）**只使用前两个子目录**；`docker/` 为非主路径的备选方案。

## 目录结构

| 子目录 / 文件 | 用途 | 消费者 | 部署落点 | 是否 eSync 同步到欧拉 |
|---|---|---|---|---|
| `nginx/ibmcp.conf` | Nginx 反代配置（`/api`、`/nodered`、静态站点、50m 上传） | `bootstrap.sh` / `deploy.sh` | `/etc/nginx/conf.d/ibmcp.conf` | ✅ 是 |
| `systemd/ibmcp-api.service` | 后端 `ibmcp_server` 的 systemd unit 模板 | `deploy.sh` | `/etc/systemd/system/ibmcp-api.service` | ✅ 是 |
| `docker/Dockerfile` | 后端单容器构建（备选方案，非主路径） | 手动 `docker build` | 镜像 `ibmcp-server` | ❌ 否（被 eSync 排除） |

## 模板占位符约定（重要）

`nginx/ibmcp.conf` 与 `systemd/ibmcp-api.service` 中**硬编码了项目根路径占位符 `/home/meng/ibmcp`**，不要直接在模板里改它。

实际部署时由脚本做运行期替换：

- `bootstrap.sh`：`sed "s#/home/meng/ibmcp#$PROJECT_ROOT#g"` 后写入 `/etc/nginx/conf.d/ibmcp.conf`；
- `deploy.sh`：安装 systemd unit 与 nginx 配置时同样按 `$PROJECT_ROOT` 替换。

因此无论项目实际克隆到哪个目录（如 `/home/meng/ibmcp`、`/opt/ibmcp`），模板都无需改动。

## nginx/ibmcp.conf

反代契约与 `backend/config.json`、前端 `baseURL` 对齐：

- `location /api/` → `127.0.0.1:8000/api/`（后端 Drogon，`config.json` 监听 8000）；
- `location /nodered/` → `127.0.0.1:1880`（Node-RED，`httpAdminRoot: /nodered`，由 `deployments/nodered/settings.js.patch` 定制）；
- `location /` → 前端静态站点 `$root/dist`；
- `client_max_body_size 50m`（后端 `config.json` 为 10M，nginx 上限更宽，后端先生效）。

## systemd/ibmcp-api.service

- `ExecStart=$PROJECT_ROOT/dist/bin/ibmcp_server`，`WorkingDirectory=$PROJECT_ROOT/dist`；
  （`config.json` 与 `migrations/` 由 CMake `install` 落到 `dist/`，`backend/main.cpp` 以相对路径 `loadConfigFile("config.json")` 读取，故 `WorkingDirectory` 必须指向 `dist/`）；
- `Restart=always`、`RestartSec=5s` 崩溃自愈；
- 依赖 `docker.service`（PG / Redis / Node-RED 跑在 compose 容器里）。

## docker/Dockerfile

**非主路径**。项目默认后端 + Nginx 不容器化（需直接访问宿主 libpq 与协议驱动 `.so`），走 `deployments/openeuler/build.sh` 裸机编译。本文件仅在「需要把后端也装进容器」的场景使用。

特点：

- 多阶段构建（builder + runtime），最终镜像只含运行时 `.so` 与后端产物；
- Drogon / Trantor 编译参数与 `deployments/openeuler/bootstrap.sh` 完全一致（见 `docs/0-Drogon/10`）；
- 协议驱动库复用项目自身的 `deployments/openeuler/build_drivers.sh`，与裸机构建口径一致（缺驱动时自动退化模拟值，不阻断构建）；
- 构建上下文为项目根：`docker build -f packaging/docker/Dockerfile -t ibmcp-server .`；
- 运行需能访问 PG / Redis：`docker run -d --network host --name ibmcp-api ibmcp-server`，或挂载自定义 `config.json` 覆盖容器内地址。

## 关联文档

- `deployments/openeuler/README.md` — 欧拉端构建 / 部署全流程；
- `deployments/README.md` — 部署目录总览；
- `docs/0-Drogon/05-部署与运行.md` / `10-环境变量与配置.md` — 约定来源。