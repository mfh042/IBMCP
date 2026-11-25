# 05 - openEuler 构建与部署

> 开发在 **Windows 11**（CLion / VSCode 编辑源码），构建与运行在 **openEuler 24.03 LTS-SP3** 虚拟机 `192.168.80.136`（用户 `meng`）。
> 所有 C++ 库与工具统一部署在用户可写目录 **`/home/meng/ibmbuild`**，不污染 `/usr/local`。
> Docker 承载 PG15 / Redis7 / Node-RED，Nginx 托管前端并反代，systemd 拉起后端单一二进制 `ibmcp_server`。

## 1. 部署拓扑

```
Windows 11 开发机                         openEuler 24.03 (192.168.80.136, user=meng)
┌─────────────────────────┐               ┌──────────────────────────────────────────┐
│ CLion / VSCode 编辑源码 │   eSync       │ /home/meng/ibmcp        项目源码         │
│ frontend/ npm run build │ ────────────► │ /home/meng/ibmbuild     C++ 库与工具     │
│                         │   (scp/rsync) │                                          │
│ euler-remote.ps1        │               │  Docker                                  │
│  e / esudo / eSync      │               │   ├ ibmcp-postgres :5432  (pg15, 库 ibmcp)│
└─────────────────────────┘               │   ├ ibmcp-redis    :6379  (redis7)       │
                                          │   └ ibmcp-nodered  :1880  (联调用)        │
                                          │  Nginx :80                                │
                                          │   ├ 静态托管 /home/meng/ibmcp/frontend-dist│
                                          │   └ 反代 /api/* 与 /ws/* → 127.0.0.1:8000 │
                                          │  systemd 服务 ibmcp-api                   │
                                          │   └ /home/meng/ibmcp/dist/bin/ibmcp_server│
                                          │       监听 127.0.0.1:8000                 │
                                          │       链接 PG / Redis（Docker）           │
                                          │       内嵌采集器（静态库，同进程）         │
                                          └──────────────────────────────────────────┘
```

## 2. 目录约定（核心，务必先理解）

### 2.1 构建根 `/home/meng/ibmbuild`

所有 C++ 依赖与工具都落在这个用户可写目录下，`backend/CMakeLists.txt` 的查找路径**硬编码**与此对齐：

```
/home/meng/ibmbuild/
├── env.sh              # 自动生成：聚合 PKG_CONFIG_PATH / LD_LIBRARY_PATH / PATH
├── drogon/             # Drogon 安装前缀（静态库 .a）
│   ├── cmake/Drogon/   #   DrogonConfig.cmake（CMake find_package 用）
│   ├── include/        #   drogon/ + trantor/ 头文件
│   ├── lib64/          #   libdrogon.a、libtrantor.a（静态！无运行时 .so 依赖）
│   ├── lib64/pkgconfig/drogon.pc
│   └── bin/drogon_ctl
├── trantor/            # Trantor 安装前缀（静态库 .a，CMake find 路径需要）
│   ├── cmake/Trantor/
│   ├── include/trantor/
│   └── lib64/libtrantor.a
├── libmodbus/          # libmodbus 前缀（动态 .so）
│   ├── lib/libmodbus.so*、lib/pkgconfig/libmodbus.pc、include/modbus/
└── open62541/          # open62541 前缀（动态 .so）
    └── lib/libopen62541.so*、lib/pkgconfig/open62541.pc、include/
```

- **mosquitto** 走 dnf 装到系统路径（无独立前缀）。
- **Drogon / Trantor 是静态库（`.a`）**：最终 `ibmcp_server` 对它们**没有运行时 `.so` 依赖**，`ldd` 里不会出现 `libdrogon.so`。运行时真正需要动态解析的只有：系统库（libpq / libssl / libcrypto / libz / libstdc++ 等）+ 协议驱动 `.so`（libmodbus / libopen62541 / libmosquitto）。
- 覆盖变量：所有脚本都接受 `IBMCP_BUILD_DIR` 环境变量覆盖默认值。

### 2.2 项目根 `/home/meng/ibmcp`

```
/home/meng/ibmcp/
├── backend/            # C++ 源码（CMakeLists.txt + src/ + include/ + collector/ + migrations/ + config.json）
├── frontend/           # Vue3 前端（Windows 上 npm run build → dist/）
├── frontend-dist/      # Nginx 托管的前端构建产物（从 frontend/dist 同步而来）
├── deployments/openeuler/   # 构建/部署脚本（本文档对应物）
├── packaging/          # nginx 配置 + systemd unit 模板
├── docs/0-Drogon/      # 本文档集
├── dist/               # 构建产物（build.sh 生成，运行时以此为工作目录）
│   ├── bin/ibmcp_server
│   ├── config.json
│   ├── migrations/
│   └── logs/
└── build/              # CMake 构建中间目录（backend/build）
```

## 3. 首次环境准备（bootstrap.sh，在欧拉上 root 执行，幂等）

```bash
cd /home/meng/ibmcp
sudo bash deployments/openeuler/bootstrap.sh
```

`bootstrap.sh` 依次完成：

| 步骤 | 内容 |
|------|------|
| 1 | 关闭 SELinux / firewalld（开发环境） |
| 2 | dnf 安装：gcc/g++/cmake/make/pkg-config/gdb、libpq-devel、libargon2-devel、openssl/zlib/jsoncpp/c-ares/yaml-cpp 开发包、nginx、git/vim/curl 等 |
| 3 | **清理旧 FastAPI systemd 单元**（若 `/etc/systemd/system/ibmcp-api.service` 含 uvicorn/venv/FastAPI 则停用并删除，消除崩溃循环） |
| 4 | 编译安装 **Drogon + Trantor（静态）** → `/home/meng/ibmbuild/{drogon,trantor}`（已存在则跳过） |
| 5 | 调用 `build_drivers.sh`：装协议驱动库 + 生成 `env.sh` + 注册 ldconfig |
| 6 | 安装 Docker CE + compose v2（已装则跳过），把当前用户加入 docker 组 |
| 7 | `docker compose up -d` 启动 PG15 / Redis7 / Node-RED 并健康检查 |
| 8 | 部署 nginx 配置、放开 `ptrace_scope`（CLion gdbserver 远程调试） |

可覆盖变量：`IBMCP_BUILD_DIR`、`DROGON_VERSION`（默认 `v1.9.5`）、`IBMCP_GITHUB_MIRROR`（默认 `https://kkgithub.com`）、`DOCKER_REPO_MIRROR`。

> GitHub 慢时设 `IBMCP_GITHUB_MIRROR=https://kkgithub.com`（已默认）或 `https://gitee.com/mirrors`。

## 4. 协议驱动库（build_drivers.sh）

`libmodbus` / `open62541` 欧拉源可能缺包，从源码编译到各自子目录；`mosquitto` 优先 dnf。任一失败仅**退化**（对应协议编译为模拟值），不阻断构建：

```bash
sudo bash deployments/openeuler/build_drivers.sh
```

完成后自动：
1. 生成 `/home/meng/ibmbuild/env.sh`，扫描 `ibmbuild/*/{lib,lib64}/pkgconfig`、`ibmbuild/*/{lib,lib64}`、`ibmbuild/*/bin` 聚合导出；
2. 写 `/etc/ld.so.conf.d/ibmcp-build.conf` 注册所有 lib 目录并 `ldconfig`。

> `env.sh` 供交互 shell / `build.sh` 显式 source；**systemd 非登录 shell 不读 env.sh，靠 ldconfig**（见 §6）。

## 5. 后端构建（build.sh）

```bash
cd /home/meng/ibmcp
bash deployments/openeuler/build.sh
```

`build.sh` 会：
1. `source /home/meng/ibmbuild/env.sh`；
2. 检查 Drogon / libpq / argon2 / 驱动库，缺失驱动则自动调用 `build_drivers.sh`；
3. `cmake -S backend -B backend/build` 配置，`cmake --build` 编译；
4. `cmake --install` 到 `dist/`，产物：
   - `dist/bin/ibmcp_server` —— 后端可执行文件（含采集器静态库 + Drogon 静态库）
   - `dist/config.json` —— 运行配置
   - `dist/migrations/` —— SQL 迁移脚本
   - `dist/logs/` —— 运行日志目录
5. 跑 `ldd dist/bin/ibmcp_server | grep not found`，有未解析库则报错退出。

### 5.1 CMake 工程（`backend/CMakeLists.txt`，实际内容）

```cmake
cmake_minimum_required(VERSION 3.20)
project(ibmcp_backend CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(IBMCP_BUILD_DIR "/home/meng/ibmbuild")          # ← 与部署布局硬绑定
list(APPEND CMAKE_PREFIX_PATH
    "${IBMCP_BUILD_DIR}/drogon/cmake"
    "${IBMCP_BUILD_DIR}/trantor/cmake")
set(ENV{PKG_CONFIG_PATH}
    "${IBMCP_BUILD_DIR}/drogon/lib64/pkgconfig:"
    "${IBMCP_BUILD_DIR}/libmodbus/lib/pkgconfig:"
    "${IBMCP_BUILD_DIR}/open62541/lib/pkgconfig:"
    "$ENV{PKG_CONFIG_PATH}")

find_package(Drogon CONFIG REQUIRED PATHS "${IBMCP_BUILD_DIR}/drogon/cmake" NO_DEFAULT_PATH)
find_package(OpenSSL REQUIRED)
find_library(PQ_LIBRARY pq)            # libpq
find_library(ARGON2_LIBRARY argon2)    # 可选，缺失则密码哈希降级
add_subdirectory(collector)            # 采集器静态库 ibmcp_collector

file(GLOB_RECURSE BACKEND_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(ibmcp_server ${BACKEND_SOURCES})
target_link_libraries(ibmcp_server PRIVATE
    Drogon::Drogon ibmcp_collector
    OpenSSL::SSL OpenSSL::Crypto ${PQ_LIBRARY})
# argon2 / zlib 按需链接；采集器通过 pkg-config 探测 modbus/mosquitto/open62541
```

> 若需改构建根，同步修改这里的 `IBMCP_BUILD_DIR`，或改为读环境变量：`set(IBMCP_BUILD_DIR "$ENV{IBMCP_BUILD_DIR}" CACHE PATH "")`。

## 6. 运行时动态库（关键）

`ibmcp_server` 是 **Drogon/Trantor 静态链接** + **驱动库动态链接** 的二进制。运行机必须能解析其动态依赖：

| 动态库 | 来源 | 解析方式 |
|--------|------|----------|
| libpq | dnf `postgresql-libs` / `libpq` | 系统默认路径 |
| libssl / libcrypto | dnf `openssl-libs` | 系统默认路径 |
| libz / libstdc++ / libgcc_s | 系统自带 | 系统默认路径 |
| libargon2 | dnf `argon2` | 系统默认路径（编译时链了才需要） |
| libmodbus.so | `/home/meng/ibmbuild/libmodbus/lib` | **ldconfig 注册** |
| libopen62541.so | `/home/meng/ibmbuild/open62541/lib` | **ldconfig 注册** |
| libmosquitto.so | dnf 系统路径 | 系统默认路径 |

`build_drivers.sh` 已把驱动 lib 目录写进 `/etc/ld.so.conf.d/ibmcp-build.conf` 并 `ldconfig`。验证：

```bash
ldd /home/meng/ibmcp/dist/bin/ibmcp_server | grep "not found"   # 必须无输出
ldconfig -p | grep -E 'libmodbus|libopen62541'
```

> 历史上欧拉已有 `ibmcp-drivers.conf` / `ibmcp-libmodbus.conf` / `ibmcp-open62541.conf`，与新的 `ibmcp-build.conf` 等价，可共存。

## 7. 运行配置 `config.json`

`main.cpp` 用 `app().loadConfigFile("config.json")` 从**当前工作目录**加载，`migrations_dir` / `log_path` 也相对工作目录。因此 **systemd 的 WorkingDirectory 必须是 `dist/`**（见 §9）。实际 `backend/config.json`（关键字段）：

```json
{
  "listeners": [{ "address": "0.0.0.0", "port": 8000 }],
  "app": { "threads_num": 4, "log": { "log_path": "./logs", "logfile_base_name": "ibmcp" } },
  "db_clients": [
    { "name": "default", "rdbms": "postgresql", "host": "127.0.0.1", "port": 5432,
      "dbname": "ibmcp", "user": "ibmcp", "passwd": "ibmcp", "number_of_connections": 5 },
    { "name": "write",   "rdbms": "postgresql", "host": "127.0.0.1", "port": 5432,
      "dbname": "ibmcp", "user": "ibmcp", "passwd": "ibmcp", "number_of_connections": 5 }
  ],
  "custom_config": {
    "app_secret_key": "dev-secret-change-me",
    "pg_dsn": "postgresql://ibmcp:ibmcp@127.0.0.1:5432/ibmcp",
    "migrations_dir": "./migrations",
    "cors_origins": ["http://localhost:5173"],
    "public_paths": ["/api/v1/auth/login", "/api/v1/ping", "/api/v1/openapi.json", ...]
  }
}
```

> 生产环境 `app_secret_key`、DB 密码应从环境变量注入或用独立 `config.json`（权限 600）覆盖；当前 `main.cpp` 不读环境变量，敏感项直接改 `dist/config.json`。

手动试跑：

```bash
cd /home/meng/ibmcp/dist
./bin/ibmcp_server          # 前台运行，Ctrl+C 退出
# 另开终端验证
curl -s http://127.0.0.1:8000/api/v1/ping
```

## 8. 一键部署（deploy.sh）

```bash
cd /home/meng/ibmcp
sudo bash deployments/openeuler/deploy.sh            # 编译 → 容器 → 迁移 → nginx → systemd → 健康校验
sudo bash deployments/openeuler/deploy.sh --full     # 含 Redis + Node-RED（默认 slim 仅 PG）
sudo bash deployments/openeuler/deploy.sh --bundle /tmp/ibmcp-bundle.tar.gz   # 离线还原
sudo bash deployments/openeuler/deploy.sh --force-migrate   # 无视已有表重跑迁移
```

`deploy.sh` 流程：编译（或 bundle 还原）→ 确保 ldconfig → 起容器 → 等 PG 就绪 → 幂等跑迁移（已有表则跳过）→ 部署前端 → 配 nginx → 装/启 systemd → 健康校验。

## 9. systemd 服务（`packaging/systemd/ibmcp-api.service`）

```ini
[Unit]
Description=IBMCP Drogon API Server (C++)
After=network.target docker.service nginx.service
Requires=docker.service

[Service]
Type=simple
User=meng
WorkingDirectory=/home/meng/ibmcp/dist
ExecStart=/home/meng/ibmcp/dist/bin/ibmcp_server
Restart=always
RestartSec=3
LimitNOFILE=65536
LimitNPROC=8192
Environment=TZ=Asia/Shanghai

[Install]
WantedBy=multi-user.target
```

`deploy.sh` 会用 `sed` 把模板里的 `/home/meng/ibmcp` 替换成实际 `PROJECT_ROOT`、`User=meng` 替换成运行用户，再安装。安装启动：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now ibmcp-api
sudo systemctl status ibmcp-api
journalctl -u ibmcp-api -f        # 看日志
```

> ⚠️ 旧版单元曾引用 `EnvironmentFile=/home/meng/ibmcp/backend/.env` 与 `venv/bin/uvicorn`，文件不存在会导致 systemd 反复崩溃重启。`bootstrap.sh` 会自动检测并清理这种旧单元；新模板已移除 `EnvironmentFile`。

## 10. Nginx（`packaging/nginx/ibmcp.conf`）

```nginx
server {
    listen 80;
    listen [::]:80;
    server_name _;
    client_max_body_size 50m;
    root /home/meng/ibmcp/frontend-dist;
    index index.html;

    location /ws/push {                # WebSocket 推送（Drogon 注册路径之一）
        proxy_pass http://127.0.0.1:8000/ws/push;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 3600s;
    }
    location /api/v1/ws/collect {      # 兼容旧路径
        proxy_pass http://127.0.0.1:8000/api/v1/ws/collect;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 3600s;
    }
    location /api/ {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
    location / { try_files $uri $uri/ /index.html; }
}
```

## 11. Windows 11 开发 → 欧拉部署工作流

### 11.1 远程工具 `deployments/openeuler/euler-remote.ps1`

在 Windows PowerShell 里 dot-source（首次先 `eInitKey` 装公钥，之后全程免密）：

```powershell
. .\deployments\openeuler\euler-remote.ps1
eInitKey          # 仅首次：把本机 ~/.ssh/id_ed25519.pub 装到欧拉
e  'uname -a'     # 远程执行
esudo 'systemctl status ibmcp-api'   # 远程 sudo（自动喂密码）
eup .\dist  /home/meng/ibmcp/frontend-dist    # 上传
edown /home/meng/ibmcp/dist/bin/ibmcp_server  .\   # 下载
```

### 11.2 源码同步 `eSync`

改完代码后一键推到欧拉（tar over ssh，自动排除 `node_modules` / `.git` / `cmake-build-*` / `backend/build` / `dist` / `frontend/dist` 等非源码目录）：

```powershell
eSync
```

同步后在欧拉上重新构建部署：

```bash
# 欧拉侧
cd /home/meng/ibmcp
bash deployments/openeuler/build.sh && sudo bash deployments/openeuler/deploy.sh
```

### 11.3 上传边界：哪些必须传 / 可传可不传 / 不必传

> 原则：**开发在 Windows 11，构建/调试/部署在欧拉**。欧拉端只接收「构建与部署的直接输入」；构建产物、本地依赖、IDE 配置一律由两端各自生成，不跨端传输。
>
> 当前 `eSync` 的排除清单（见 `euler-remote.ps1`）已精确实现这一边界，本节是它的文字说明与判定依据。

#### 11.3.1 必须上传（构建/部署的直接输入）

| 文件 / 目录 | 被谁引用 | 说明 |
|---|---|---|
| `backend/`（整个目录） | `build.sh` → `cmake -S backend` | 后端编译全部输入 |
| └ `backend/CMakeLists.txt` | CMake | 构建入口 |
| └ `backend/src/` | `GLOB_RECURSE src/*.cpp` | 全部实现 |
| └ `backend/include/` | `target_include_directories` | 头文件 |
| └ `backend/collector/` | `add_subdirectory(collector)` | 采集器静态库，编进 `ibmcp_server` |
| └ `backend/migrations/` | `cmake --install` + `deploy.sh` | SQL 迁移，装到 `dist/migrations/` |
| └ `backend/config.json` | `cmake --install` + 运行时 | 运行配置，装到 `dist/` |
| 根 `CMakeLists.txt` | `cmake` | `project(IBMCP)` + `add_subdirectory(backend)` |
| `deployments/openeuler/*.sh` | 欧拉端执行 | `build.sh` / `deploy.sh` / `build_drivers.sh` / `bootstrap.sh` / `check_toolchain.sh` / `dev-env.sh` / `make-bundle.sh` |
| `deployments/openeuler/docker-compose.yml` | `deploy.sh` | 起 PG / Redis / Node-RED 容器 |
| `packaging/nginx/ibmcp.conf` | `deploy.sh` | nginx 反代模板（`sed` 替换路径后 cp 到 `/etc/nginx/conf.d/`） |
| `packaging/systemd/ibmcp-api.service` | `deploy.sh` | systemd unit 模板（`sed` 替换后 cp 到 `/etc/systemd/system/`） |
| `frontend/dist/`（构建产物） | `deploy.sh --frontend-dist` | 前端**只传产物**，部署到 `$PROJECT_ROOT/frontend-dist/` |

#### 11.3.2 不必上传（欧拉端自生成 / 纯本地）

| 文件 / 目录 | 原因 |
|---|---|
| `/home/meng/ibmbuild/`（drogon / trantor / 驱动库 + `env.sh`） | 由 `bootstrap.sh` / `build_drivers.sh` 在欧拉端编译生成，**只在欧拉存在**，从不从 Windows 传 |
| 系统包（gcc / cmake / docker / nginx / libpq …） | dnf 安装，不传 |
| `backend/build/`、`cmake-build-*/`、`dist/` | 构建产物 / 缓存，欧拉端 `build.sh` 自己生成 |
| `compile_commands.json` | CMake 生成，欧拉端重建 |
| `frontend/src/`、`frontend/node_modules/`、`frontend/package.json`、`frontend/*.config.*` | **前端在 Windows 构建**（`deploy.sh` 明确「请在 Windows 上 npm run build 后上传」），欧拉端不需要前端源码 |
| `euler-remote.ps1` | 这是 **Windows 侧**的远程工具，在本机运行，不上传欧拉 |
| `.idea/`、`.vscode/`、`.trae/` | IDE / 工具本地配置 |
| `*.log`、临时文件、`_fix_temp/` 等 | 无意义 |

#### 11.3.3 可传可不传（传了无害，不影响构建/部署）

| 文件 / 目录 | 说明 |
|---|---|
| `docs/` | 不传不影响运行；传上去方便在欧拉端查阅。建议保留同步 |
| `backend/collector/tests/` | 单测代码；`backend/CMakeLists.txt` **没有** `add_subdirectory(tests)`，不编进主二进制，只有要在欧拉跑单测时才需要 |
| `packaging/docker/Dockerfile` | 当前后端是欧拉本地 `g++` 编译，**不走 docker 构建**，用不到；只有改成容器化构建才需要 |
| `deployments/openeuler/make-bundle.sh` | 离线打包辅助，仅在做离线 bundle 时用 |

#### 11.3.4 确认：`eSync` 已实现此边界

按「只传必须项」的原则，`euler-remote.ps1` 中 `eSync` 的排除清单同时覆盖「不必传」与「可传可不传」两类（11.3.2 / 11.3.3）：

```
# 版本控制 / IDE / 本地工具配置（纯本地，不传欧拉）
.git, .idea, .vscode, .trae
# 前端：在 Windows 侧构建，源码与依赖都不传；dist 产物单独上传（eup）
frontend, node_modules
# 构建产物 / 缓存（欧拉端 build.sh 自生成）
cmake-build-*, backend/build, build, build2, dist, compile_commands.json
# 远程工具（仅在 Windows 侧运行，不上传欧拉）
euler-remote.ps1
# 文档（可选，不传以瘦身同步包）
docs
# 单测（不编进主二进制，不在欧拉跑单测则不传）
backend/collector/tests
# 可选辅助：离线打包 / docker 构建（当前流程不用）
packaging/docker, make-bundle.sh
# 临时 / 日志 / marker / IDE 截图
_fix_temp, *.log, .clion.source.upload.marker, .remote-include, .screenshots
```

即 `eSync` 只同步「必须传」的源码 + 脚本 + 模板（实测约 160 个文件），其余一律排除。注意整个 `frontend/` 目录都被排除——前端产物走单独上传（`eup frontend\dist /home/meng/ibmcp/frontend-dist` 或 `deploy.sh --frontend-dist`），不与源码混传。若日后需要在欧拉端查阅文档或跑单测，从上述排除清单临时移除对应项即可。

流向示意：

```
Windows 11（开发）                         openEuler（构建/部署）
─────────────────                         ─────────────────────
backend/{src,include,collector,migrations,config.json}
CMakeLists.txt, deployments/openeuler/*.sh          eSync
packaging/{nginx,systemd}              ────────────►  /home/meng/ibmcp/{backend,deployments,packaging}
                                                       │
frontend/src  ──npm run build──► frontend/dist  ──单独上传──►  frontend-dist/
                                                       │
                                              build.sh → 编译 ibmcp_server
                                              deploy.sh → 容器 + 迁移 + nginx + systemd
                                                       ▼
                              /home/meng/ibmbuild/（欧拉本地生成，不传）
                              drogon / trantor / 驱动库 + env.sh

✗ 不传：frontend/ 源码与 node_modules、docs、collector/tests、
       packaging/docker、make-bundle.sh、.trae/.idea/.vscode、
       euler-remote.ps1、build 缓存、compile_commands.json
```

### 11.4 前端构建与部署

前端在 Windows 上构建（`frontend/`），产物 `frontend/dist/`，再同步到欧拉的 `frontend-dist/`：

```powershell
# Windows
cd frontend; npm install; npm run build
# 同步时 eSync 会排除整个 frontend/，需单独上传 dist：
eup frontend\dist  /home/meng/ibmcp/frontend-dist
```

或在欧拉侧让 `deploy.sh` 接收本地路径：`deploy.sh --frontend-dist <上传后的目录>`。

### 11.5 CLion 远程工具链（可选）

项目已配 `cmake-build-debug-oe-remote`，CLion 可直接在欧拉上远程编译/调试（gdbserver）。`bootstrap.sh` 已放开 `kernel.yama.ptrace_scope=0`。远程 CMake 用的 `IBMCP_BUILD_DIR` 与本布局一致，无需额外配置。

## 12. 单元测试（GoogleTest）

`backend/collector/tests/` 用 GoogleTest。在欧拉上：

```bash
cd /home/meng/ibmcp/backend
source /home/meng/ibmbuild/env.sh
cmake -B build -S . -DBUILD_TESTING=ON
cmake --build build -j"$(nproc)"
cd build && ctest --output-on-failure
```

## 13. 验证清单

```bash
# 1. 运行库完整性（必须无 not found）
ldd /home/meng/ibmcp/dist/bin/ibmcp_server | grep "not found" || echo "✅ 运行库齐全"
ldconfig -p | grep -E 'libmodbus|libopen62541'

# 2. 容器
docker compose -f /home/meng/ibmcp/deployments/openeuler/docker-compose.yml ps

# 3. 后端存活（绕过 nginx 直连）
curl -s http://127.0.0.1:8000/api/v1/ping
# → {"code":0,"message":"ok",...}

# 4. 登录拿 token
TOKEN=$(curl -s -X POST http://127.0.0.1:8000/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"admin123"}' | jq -r .data.access_token)

# 5. 鉴权接口
curl -s http://127.0.0.1:8000/api/v1/auth/me -H "Authorization: Bearer $TOKEN"

# 6. 经 nginx 访问前端
curl -I http://192.168.80.136/    # → 200，返回 index.html

# 7. WebSocket
wscat -c "ws://192.168.80.136/ws/push?token=$TOKEN"
```

预期：
- `ping` 返回 `code:0`；未带 token 访问鉴权接口返回 `code:401`（HTTP 仍 200）；
- 前端 `http://192.168.80.136/` 可打开并登录；
- WS 连接成功后能收到采样推送。

## 14. 日志与排障

| 问题 | 排查 |
|------|------|
| 服务起不来 | `journalctl -u ibmcp-api -n 100 --no-pager`；检查 `dist/` 下是否有 `config.json`、`migrations/`；DB 连通性 |
| `error while loading shared libraries` | `ldd dist/bin/ibmcp_server \| grep not found`；缺驱动库 → 跑 `sudo bash build_drivers.sh && sudo ldconfig`；缺系统库 → `dnf provides <库>` |
| systemd 反复重启 | 多半是旧 FastAPI 单元残留（引用不存在的 `.env`/`uvicorn`）→ `bootstrap.sh` 会清理，或手动 `sudo systemctl stop ibmcp-api && sudo rm /etc/systemd/system/ibmcp-api.service && sudo systemctl daemon-reload` 后重跑 `deploy.sh` |
| DB 连不上 | `docker exec ibmcp-postgres pg_isready -U ibmcp -d ibmcp`；确认 `config.json` 的 host/port；防火墙/SELinux |
| 收不到采样 | `/api/v1/collector/health` 看 `point_count`；检查配置是否已 activate；`dist/logs/ibmcp*.log` |
| WS 连不上 | nginx `proxy_read_timeout` 与 Upgrade 头；确认 token 传参方式；Drogon WS 路径为 `/ws/push` 与 `/api/v1/ws/collect` |
| `meng` 不能直接调 docker | `meng` 刚加入 docker 组，需**重新登录**才免 sudo；临时用 `sudo docker` |

## 15. 生产加固（可选）

- 后端监听改 `127.0.0.1`（`config.json` listeners），仅 nginx 对外；
- `app_secret_key`、DB/Redis 密码改强值，`dist/config.json` 权限 `600`；
- 数据库定时备份：`pg_dump` + cron；
- 前端构建注入 API 基址（相对路径 `/api/v1`，无需改）；
- 考虑 HTTPS：nginx 加证书，`proxy_set_header X-Forwarded-Proto https`；
- 离线分发：`make-bundle.sh` 打 `dist/` + `ibmbuild/`（+ 可选镜像），目标机 `deploy.sh --bundle` 还原（目标机仍需同版本 openEuler 与系统库）。
