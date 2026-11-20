# IBMCP 开发调试环境 · 欧拉 VMware 虚拟机服务方案

>
版本：v6.1　　编写日期：2026-11-20　　配套引用：[01-架构设计](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/01-IBMCP-FastAPI重构架构设计.md) · [02-数据模型](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/02-数据模型与字段契约（FastAPI版）.md) · [04-驱动契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/04-采集扩展驱动与协议契约（FastAPI版）.md) · [05-绑定契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md) · [07-前端契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/07-Vue3前端架构与多端交付契约.md)
>
> **本文档定位**：给出一套**开箱即用**的开发调试环境方案。核心原则 —— 所有服务部署在 VMware openEuler 虚拟机里；Windows 11
> 只做编辑器和浏览器。
>
> **服务部署架构（混合模式）**：
>
> | 部署方式           | 服务                                                                                                  | 原因                                                                                                                     |
> |--------------------|-------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------|
> | **Docker Compose** | PostgreSQL 15 · Redis 7 · Node-RED                                                                    | 纯服务容器，无编译依赖，容器化最省心；数据卷持久化                                                                       |
> | **裸机 dnf**       | Python 3.11 + venv · FastAPI · C++ 扩展（`_core.so`） · Nginx · gcc-c++ · cmake · libmodbus/open62541 | pybind11 扩展必须用宿主机器的 Python 编译，且必须直接 import 宿主机器的 .venv；Nginx 需要托管前端静态文件 + 反代 FastAPI |
>
> **远程开发模式：SSH 远程解释器 + Deployment 自动同步**（非 JetBrains Gateway Remote Development）。代码源头在 Windows，欧拉侧是
> PyCharm 自动同步的镜像副本。

---

## 〇、欧拉是否需要完整代码？（核心问题解答）

**需要。但不需要你手动维护。**

本项目采用 **SSH 远程解释器 + Deployment 自动同步** 模式：

| 对比项               | 本项目方案（SSH 远程解释器）                               | JetBrains Gateway Remote Development |
|----------------------|------------------------------------------------------------|--------------------------------------|
| **代码源头**         | **Windows**（`E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP`） | 欧拉                                 |
| **欧拉侧代码**       | PyCharm 自动同步的镜像副本（`/opt/ibmcp`）                 | 唯一源头                             |
| **谁负责同步**       | PyCharm Deployment（保存即 upload）                        | 不需要同步（直接远程编辑）           |
| **Windows 本地索引** | ✅ 有（PyCharm 在 Windows 本地建索引，速度快）             | ❌ 无（索引在欧拉侧，依赖网络）      |
| **git 操作在哪**     | Windows（本地 git）                                        | 欧拉                                 |
| **适合场景**         | Windows 性能好、想在本地享受快速索引和 git                 | 代码体量极大、Windows 性能弱         |

**结论**：欧拉虚拟机里**会有一份完整的代码副本**（在 `/opt/ibmcp`），但这份副本由 PyCharm Deployment 自动维护——你在 Windows
编辑、保存，PyCharm 自动把改动 upload 到欧拉。你**不需要**手动 scp 或 git clone 到欧拉，也**不应该**在欧拉侧手动改代码（会被下次同步覆盖）。

> **唯一例外**：首次初始化时，需要手动把项目同步一次到欧拉（见 §5.1.3），之后就是全自动。

---

## 一、整体架构与职责划分

### 1.1 开发侧拓扑

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Windows 11 开发主机（代码源头）                                          │
│                                                                         │
│  E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\                              │
│  ├─ backend/app/          ← PyCharm 编辑（本地索引）                     │
│  ├─ frontend/             ← PyCharm 编辑（本地索引）                     │
│  ├─ collector-cpp/        ← CLion 编辑（本地索引）                       │
│  ├─ python/               ← industrial_collector 包                     │
│  ├─ deployments/openeuler/ ← bootstrap.sh + build.sh + docker-compose   │
│  └─ pyproject.toml                                                   │
│                                                                         │
│  ┌──────────────────┐  ┌──────────────┐  ┌───────────────────────────┐ │
│  │ PyCharm Prof     │  │ CLion        │  │ Chrome / 数据库工具        │ │
│  │ (Python + 前端   │  │ (C++ 扩展    │  │  DBeaver / Redis Insight   │ │
│  │  + DB 面板)      │  │  + gdbserver)│  │  / npm script)            │ │
│  └────────┬─────────┘  └──────┬───────┘  └──────────────┬────────────┘ │
│           │ SSH                 │ SSH                       │ HTTP/WS    │
│           │ (远程解释器+Deployment) │ (Remote Host+SFTP)      │            │
│           ▼                    ▼                           ▼             │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  SSH 隧道层（Windows 本地端口 → 欧拉虚拟机端口）                  │   │
│  │   127.0.0.1:5432  →  euler:5432    （PG15 Docker 容器）          │   │
│  │   127.0.0.1:6379  →  euler:6379    （Redis Docker 容器）         │   │
│  │   127.0.0.1:8000  →  euler:8000    （FastAPI + WS）              │   │
│  │   127.0.0.1:5678  →  euler:5678    （PyCharm 远程 Python 调试）  │   │
│  │   127.0.0.1:1234  →  euler:1234    （gdbserver / CLion C++ 调试）│   │
│  │   127.0.0.1:5173  →  euler:5173    （Vite dev server，可选）     │   │
│  └─────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ VMware 虚拟交换机（桥接模式）
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  openEuler 24.03 LTS 虚拟机（IP: 192.168.80.134，桥接到 Windows 网段）   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ Docker Compose 层（纯服务容器，docker compose up -d 管理）       │   │
│  │  ├─ ibmcp-postgres (PG15)   :5432  ibmcp/ibmcp/ibmcp            │   │
│  │  │   └─ 卷 ibmcp_pg_data → /var/lib/postgresql/data            │   │
│  │  ├─ ibmcp-redis (Redis 7)   :6379  appendonly AOF               │   │
│  │  │   └─ 卷 ibmcp_redis_data → /data                             │   │
│  │  └─ ibmcp-nodered (Node-RED) :1880  PLC 模拟器/协议探针          │   │
│  │      └─ 卷 ibmcp_nodered_data → /data                           │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ 裸机层（Host OS）—— FastAPI + C++ 扩展 + Nginx                     │   │
│  │  ├─ /opt/ibmcp/             ← PyCharm Deployment 自动同步的镜像    │   │
│  │  │  ├─ backend/app/         ← PyCharm upload                      │   │
│  │  │  ├─ frontend/            ← PyCharm upload（源码）              │   │
│  │  │  ├─ collector-cpp/       ← CLion SFTP upload                   │   │
│  │  │  ├─ python/              ← industrial_collector 包             │   │
│  │  │  ├─ pyproject.toml                                           │   │
│  │  │  ├─ venv/                ← Python 3.11 虚拟环境               │   │
│  │  │  │  └─ industrial_collector/_core.so  ← 本机编译产出           │   │
│  │  │  ├─ frontend-dist/       ← npm run build 产出（Nginx root）   │   │
│  │  │  └─ uvicorn (PID X)      ← FastAPI（裸机，绑定 0.0.0.0:8000） │   │
│  │  ├─ Python 3.11 + venv                                           │   │
│  │  ├─ gcc-c++ + cmake（C++17）                                      │   │
│  │  ├─ libmodbus-devel / open62541-devel / mosquitto-devel           │   │
│  │  ├─ Nginx（裸机，systemd 管理，端口 80，反代 FastAPI + 静态前端） │   │
│  │  ├─ Docker Engine + compose 插件                                  │   │
│  │  ├─ gdbserver (端口 1234)                                        │   │
│  │  └─ debugpy (端口 5678)                                           │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ 现场设备 / PLC（Modbus/OPC UA/MQTT/Socket）                       │   │
│  │ 虚拟机通过桥接网络直接访问现场设备 IP                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 职责边界

| 侧                   | 做什么                                                                                                                                                                                     | 不做什么                                                             |
|----------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------|
| **Windows 11**       | 写 Python 源码（PyCharm）、写 C++ 源码（CLion）、写前端（PyCharm）、本地 git 操作、浏览器看实时数据、gdb 远程调 C++、Python 远程调试                                                       | 不编译 `_core` 扩展（无 libmodbus/open62541）、不跑 PG/Redis/FastAPI |
| **openEuler Docker** | 跑 PostgreSQL 15 + Redis 7 + Node-RED（`docker compose up -d`）                                                                                                                            | 不跑 FastAPI/C++ 扩展（编译依赖宿主）、不跑 Nginx（需托管前端静态）  |
| **openEuler 裸机**   | 接收 PyCharm/CLion 自动同步的代码镜像、编译 C++ 扩展（`pip install ./python ./backend`）、跑 FastAPI + 真扩展、跑 Nginx（反代 + 前端静态）、gdbserver 附着、debugpy 监听、现场设备协议联调 | 不做代码源头（编辑在 Windows）、不跑 PG/Redis/Node-RED（走 Docker）  |

### 1.3 为什么 Python/FastAPI 不放进 Docker？

这是**硬约束**，不是设计选择：

```
Docker 容器里的 Python
    │
    ├─ 容器里没有 libmodbus.so / libopen62541.so
    │  → 容器里无法编译 pybind11 扩展
    │
    ├─ 就算从宿主挂载 .so 进容器
    │  → .so 依赖宿主的 glibc 版本
    │  → 容器的 glibc 和宿主 openEuler 不兼容
    │  → ImportError: wrong ELF class 或 GLIBC_2.xx not found
    │
    └─ 就算版本对齐了
       → C++ 扩展调用 io_threads 时要访问宿主的 /dev/ttyUSB*、现场 PLC
       → Docker bridge 网络默认不允许容器访问宿主所在的物理网段
       → 容器要加 --network host 才能访问现场设备
       → 但加了 host 之后容器和宿主端口冲突
       → 这时候 Docker 等于没用，还增加了复杂度
```

结论：**FastAPI + pybind11 扩展必须在宿主裸机上跑**。但 PG/Redis/Node-RED 是纯服务，没有任何编译依赖和硬件访问需求，放进
Docker 最省心——数据卷持久化、一键启停、版本隔离、不污染宿主的 systemd。Nginx 也留在裸机，因为它要托管前端静态文件（
`/opt/ibmcp/frontend-dist`）和反代 FastAPI，与宿主文件系统耦合。

> **混合模式划分依据**：
>
> | 服务                     | 部署方式   | 理由                                                                  |
> |--------------------------|------------|-----------------------------------------------------------------------|
> | PostgreSQL 15            | **Docker** | 纯服务，无编译依赖，数据卷持久化，版本可随 `docker-compose.yml` 固定  |
> | Redis 7                  | **Docker** | 纯服务，AOF 持久化走数据卷                                            |
> | Node-RED                 | **Docker** | PLC 模拟器/协议探针，开发辅助工具，容器化便于随时启停                 |
> | FastAPI + `_core.so`     | **裸机**   | pybind11 扩展必须用宿主 Python 编译，且要访问 libmodbus.so / 现场 PLC |
> | Nginx                    | **裸机**   | 托管前端静态文件 + 反代 FastAPI，与宿主文件系统耦合                   |
> | Python 3.11 + venv       | **裸机**   | FastAPI 的运行时，必须与 `_core.so` ABI 一致                          |
> | gcc-c++ / cmake / 驱动库 | **裸机**   | 编译 `_core.so` 的工具链                                              |

---

## 二、欧拉虚拟机基础配置

### 2.1 首次创建虚拟机时的关键参数

| 参数 | 值                                | 理由                                                                           |
|------|-----------------------------------|--------------------------------------------------------------------------------|
| 系统 | **openEuler 24.03 LTS**（x86_64） | 与 `packaging/docker/Dockerfile` 基础镜像 `openeuler/openeuler:24.03-lts` 一致 |
| CPU  | ≥ 4 核                            | FastAPI + C++ 编译 + PG/Redis 至少 4 核                                        |
| 内存 | ≥ 8 GB                            | PG ~1.5 GB + Redis + FastAPI + venv + 编译临时                                 |
| 磁盘 | ≥ 120 GB                          | PG 时序数据增长 + C++ 编译临时 + Python venv + Nginx                           |
| 网络 | **桥接模式**（Bridged）           | 虚拟机必须获得独立 IP 才能访问现场 PLC                                         |
| SSH  | 安装时开启                        | PyCharm 远程解释器 / CLion Remote Host                                         |

### 2.2 开机后基础配置

```bash
# ——— ① 静态 IP（确认网卡名后设置）———
ip link show                                         # 找网卡名（如 ens33 / enp0s3）
nmcli connection show                                # 找连接名

# 设为静态 IP 192.168.80.134（根据你的网段调整 gateway/dns）
nmcli connection modify "ens33" \
    ipv4.method manual \
    ipv4.addresses 192.168.80.134/24 \
    ipv4.gateway 192.168.80.2 \
    ipv4.dns "192.168.80.2 8.8.8.8"
nmcli connection up "ens33"
ping -c 3 192.168.80.2 && ping -c 3 8.8.8.8          # 验证
```

```bash
# ——— ② 主机名 + 时区 ———
hostnamectl set-hostname euler-ibmcp
echo "192.168.80.134 euler-ibmcp" >> /etc/hosts
timedatectl set-timezone Asia/Shanghai
systemctl enable --now chronyd
chronyc sources
```

```bash
# ——— ③ 升级 + 基础工具 ———
dnf update -y
dnf install -y git wget curl vim-enhanced net-tools \
               chrony tar gzip zip unzip \
               htop iotop lsof strace openssh-server
systemctl enable --now sshd
```

```bash
# ——— ④ 开发期直接关防火墙 + SELinux（或只放行必需端口）———
systemctl stop firewalld
systemctl disable firewalld
setenforce 0 2>/dev/null || true
sed -i 's/^SELINUX=enforcing$/SELINUX=disabled/' /etc/selinux/config 2>/dev/null || true
```

### 2.3 SSH 免密登录（Windows → 欧拉）

```powershell
# —— Windows PowerShell ——
ssh-keygen -t ed25519 -C "ibmcp-dev" -f "$env:USERPROFILE\.ssh\ibmcp_euler"

# 推送公钥（开发用户 meng，根据你的实际用户调整）
type "$env:USERPROFILE\.ssh\ibmcp_euler.pub" | ssh -p 22 meng@192.168.80.134 `
    "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"

# 验证
ssh -i "$env:USERPROFILE\.ssh\ibmcp_euler" meng@192.168.80.134 "hostname"
```

```powershell
# 写入 Windows SSH config（PyCharm 远程解释器 / CLion 自动识别）
# 编辑: $env:USERPROFILE\.ssh\config
# 加入：
# Host euler-ibmcp
#     HostName 192.168.80.134
#     User meng
#     Port 22
#     IdentityFile ~/.ssh/ibmcp_euler
#     ServerAliveInterval 30
#     ServerAliveCountMax 3
```

> **用户说明**：PyCharm SSH 配置（`sshConfigs.xml`）中开发连接用用户 `meng`。但 `deployments/openeuler/bootstrap.sh` 需要
> `root` 权限执行（`sudo bash bootstrap.sh`），因为它要装系统包、装 Docker、配 Nginx systemd。开发期日常 SSH/PyCharm 用 `meng`
> ，首次 bootstrap 用 `root` 或 `sudo`。

---

## 三、欧拉服务安装（一键脚本，混合模式）

### 3.1 使用项目自带的 bootstrap.sh

本项目在 `deployments/openeuler/bootstrap.sh` 中提供了一键环境初始化脚本，采用**混合模式**：PG/Redis/Node-RED 走 Docker
Compose，FastAPI/C++/Nginx 裸机安装。

```bash
# —— 在欧拉虚拟机上执行（需要 root）——
# 先把项目同步一次到欧拉（首次，见 §5.1.3 用 PyCharm Deployment 或手动 scp）
# 假设项目已在 /opt/ibmcp

sudo bash /opt/ibmcp/deployments/openeuler/bootstrap.sh
```

脚本会自动完成以下 7 步：

| 步骤 | 内容                                                                                                                                                                                               |
|------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1/7  | 关闭 SELinux + 防火墙                                                                                                                                                                              |
| 2/7  | dnf 安装裸机编译工具链：gcc/g++/cmake/python3-devel/postgresql-devel/pybind11-devel/libmodbus-devel/mosquitto-devel/open62541-devel/nginx/git/wget/curl 等（**不装 postgresql/redis，走 Docker**） |
| 3/7  | 安装 Docker + docker compose 插件，启动 docker 守护进程，当前用户加入 docker 组                                                                                                                    |
| 4/7  | `docker compose up -d` 启动 PG15 + Redis 7 + Node-RED 三个容器（用 `deployments/openeuler/docker-compose.yml`），等待健康检查通过                                                                  |
| 5/7  | 部署 Nginx 配置（`packaging/nginx/ibmcp.conf` → `/etc/nginx/conf.d/ibmcp.conf`），创建前端静态目录 `/opt/ibmcp/frontend-dist`，启动 Nginx + 开机自启                                               |
| 6/7  | 安装 uv（Python 包管理器）                                                                                                                                                                         |
| 7/7  | 放开 ptrace_scope（CLion gdbserver 远程调试需要）                                                                                                                                                  |

### 3.2 docker-compose.yml 与 PG 口径

`deployments/openeuler/docker-compose.yml` 定义了三个容器，PG 口径与 `backend/.env.example`、`backend/app/core/config.py`
**完全一致**：

| 配置项    | 值                                              | 来源                                                    |
|-----------|-------------------------------------------------|---------------------------------------------------------|
| PG 用户   | `ibmcp`                                         | docker-compose.yml（`POSTGRES_USER`）+ .env.example     |
| PG 密码   | `ibmcp`                                         | docker-compose.yml（`POSTGRES_PASSWORD`）+ .env.example |
| PG 数据库 | `ibmcp`                                         | docker-compose.yml（`POSTGRES_DB`）+ .env.example       |
| PG_HOST   | `127.0.0.1`                                     | .env.example + config.py（容器端口映射到宿主 5432）     |
| PG_PORT   | `5432`                                          | .env.example + config.py（容器 5432 → 宿主 5432）       |
| DSN 格式  | `postgresql://ibmcp:ibmcp@127.0.0.1:5432/ibmcp` | config.py `pg_dsn` 属性                                 |
| Redis URL | `redis://127.0.0.1:6379/0`                      | .env.example（容器 6379 → 宿主 6379）                   |

> **注意**：PG/Redis 容器端口直接映射到宿主的 `127.0.0.1`，所以 FastAPI（跑在裸机上）用 `PG_HOST=127.0.0.1` 就能连到容器里的
> PG，无需特殊网络配置。

> **数据持久化**：三个容器的数据分别存在 Docker 命名卷 `ibmcp_pg_data`、`ibmcp_redis_data`、`ibmcp_nodered_data` 中。
> `docker compose down` 不会删除卷，`docker compose down -v` 才会（慎用，会丢 PG 数据）。

### 3.3 bootstrap 完成后的验证

```bash
# Docker 容器状态
cd /opt/ibmcp
docker compose -f deployments/openeuler/docker-compose.yml ps
# 应看到 ibmcp-postgres / ibmcp-redis / ibmcp-nodered 三个容器 Up

# PG 验证（容器内）
docker exec ibmcp-postgres pg_isready -U ibmcp -d ibmcp
# 或从宿主用密码连接（FastAPI 的连法）
PGPASSWORD=ibmcp psql -U ibmcp -h 127.0.0.1 -d ibmcp -c "\dt"

# Redis 验证（容器内）
docker exec ibmcp-redis redis-cli ping          # PONG

# Node-RED 验证
curl -s http://127.0.0.1:1880 | head -1          # 有 HTML 返回即正常

# Nginx 验证（裸机）
systemctl status nginx                           # active (running)

# Python + 编译工具链（裸机）
python3 --version
gcc --version | head -1
cmake --version | head -1
```

---

## 四、编译 IBMCP C++ 扩展 + venv

### 4.1 使用项目自带的 build.sh

`deployments/openeuler/build.sh` 负责创建 venv + 编译 C++ 扩展 + 安装 backend：

```bash
# —— 在欧拉虚拟机上执行 ——
cd /opt/ibmcp
bash deployments/openeuler/build.sh
```

脚本内容（精简）：

```bash
# 创建 venv
python3.11 -m venv venv
source venv/bin/activate

# 安装 C++ 编译依赖（幂等）
dnf install -y cmake gcc-c++ python3.11-devel || true

# 编译并安装 industrial_collector（含 _core.so）+ backend
pip install ./python ./backend
```

### 4.2 C++ 编译链说明

| 组件         | 版本要求             | 说明                                                                 |
|--------------|----------------------|----------------------------------------------------------------------|
| gcc-c++      | openEuler 24.03 自带 | 支持 C++17                                                           |
| cmake        | ≥ 3.20               | `CMakeLists.txt` 要求 `cmake_minimum_required(VERSION 3.20)`         |
| **C++ 标准** | **C++17**            | `collector-cpp/CMakeLists.txt` 第 5 行：`set(CMAKE_CXX_STANDARD 17)` |
| pybind11     | pybind11-devel       | `find_package(pybind11 CONFIG REQUIRED)`                             |
| libmodbus    | libmodbus-devel      | `pkg_check_modules(MODBUS QUIET libmodbus)`                          |
| mosquitto    | mosquitto-devel      | `pkg_check_modules(MOSQUITTO QUIET libmosquitto)`                    |
| open62541    | open62541-devel      | `pkg_check_modules(OPEN62541 QUIET open62541)`（需 1.4+）            |

> **C++ 标准是 C++17，不是 C++14**。`CMakeLists.txt` 明确设置 `CMAKE_CXX_STANDARD 17` + `CMAKE_CXX_STANDARD_REQUIRED ON`。

### 4.3 冒烟测试

编译完成后，验证 `_core.so` 扩展能加载：

```bash
cd /opt/ibmcp
source venv/bin/activate

# 验证 C++ 扩展直接加载
python -c "import industrial_collector._core as c; print('✅ _core loaded'); print(c.health())"

# 验证通过 NativeCollector 封装调用
python -c "
from app.core.collector.ffi import NativeCollector
nc = NativeCollector()
nc.init({})
print('✅ NativeCollector OK, protocols:', nc.list_protocols())
print('health:', nc.health())
nc.shutdown()
"
```

> **注意 API 路径**：代码里没有 `from industrial_collector import Collector`。正确路径是：
> - C++ 扩展模块：`industrial_collector._core`（pybind11 编译产出）
> - Python 封装层：`app.core.collector.ffi.NativeCollector`（`backend/app/core/collector/ffi.py`）
> - 接口协议：`app.core.collector.protocol.CollectorApi`（`Protocol` 类，定义了
>   `init/health/list_protocols/apply_plan/snapshot_since/write_point/shutdown` 等方法签名）
>
> `CollectorApi` 接口契约（方法签名、参数、返回值、GIL 规则、stub
> 实现）全部以 [05-pybind11绑定与C++Python分层契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md)
> 为唯一权威，本文档只保留开发环境的编译和冒烟测试步骤。

### 4.4 COLLECTOR_BACKEND 的取值

`backend/app/core/config.py` 第 29 行定义：

```python
collector_backend: str = Field(default="stub", description="stub | native")
```

| 取值     | 含义                                                   | 使用场景                     |
|----------|--------------------------------------------------------|------------------------------|
| `stub`   | `StubCollector`（纯 Python，无 `_core.so`）            | Windows 开发、CI、无硬件联调 |
| `native` | `NativeCollector`（加载 `industrial_collector._core`） | 欧拉裸机、真实采集           |

> **不是 `ffi`**。`backend/app/core/collector/ffi.py` 的 `get_collector(backend)` 函数接受 `"stub"` 或 `"native"`，其他值抛
> `ValueError`。

---

## 五、Windows 远程开发配置

### 5.1 PyCharm Professional（Python + 前端 + DB 三合一）

**前置条件**：Windows 安装 PyCharm Professional 2026.3（Deployment / SSH 远程解释器为 Pro 版功能；Community 版无此功能）

#### 5.1.1 配置 SSH 远程解释器（非 Gateway）

```
Ctrl+Alt+S → Python | Interpreter → Add Interpreter → On SSH

步骤 1：选 "Existing" → 从下拉列表选已配好的 SSH 配置（euler-ibmcp）
        或选 "New" → 填 Host: 192.168.80.134 / Port: 22 / Username: meng
        → 认证方式选 Key pair (OpenSSH) → Private key: ~/.ssh/ibmcp_euler
        → Next → 等待 PyCharm 完成 SSH 服务器探测

步骤 2：Interpreter path: /opt/ibmcp/venv/bin/python3.11
        （PyCharm 会自动探测可用的 Python 环境，选 virtualenv → 指定路径）

步骤 3：Sync folder:
        Local path:  E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP
        Remote path: /opt/ibmcp

步骤 4：PyCharm 右下角弹出同步提示气泡：
        → 点 "Sync and auto-upload files"
          （立即同步 + 之后每次保存自动上传）
        → 或点 "Auto-upload files"（仅开启自动上传，不立即同步）

步骤 5：PyCharm 自动在欧拉上创建 ~/.cache/JetBrains/ 辅助文件
```

> **关键区别 vs Gateway**：SSH 远程解释器模式下，PyCharm **在 Windows 本地建索引**（代码在 `E:\999-Dev\...\02-IBMCP`
> ），只在运行/调试时通过 SSH 连欧拉的解释器。每次保存文件，Deployment 自动 upload 到 `/opt/ibmcp`。这比 Gateway
> 的"全远程索引"快得多，因为索引不依赖网络。

#### 5.1.2 配置 Deployment（自动同步）

```
Ctrl+Alt+S → Build, Execution, Deployment | Deployment
  （或菜单：Tools | Deployment | Configuration）

步骤 1：点左上角 + → 选 SFTP → 弹出 Create New Server → 输入名称 "euler-ibmcp" → OK

步骤 2：Connection 标签页：
    SSH configuration: 点 ... 浏览 → 选已配好的 SSH 配置
                       （或新建：Host 192.168.80.134 / Port 22 / User meng / Key pair）
    Root path: /opt/ibmcp
        （可点 Autodetect 自动探测，或手动填 + 点 Browse 确认）
    Web server URL: （留空，本项目不需要）

步骤 3：Mappings 标签页：
    Local path:        E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP
    Deployment path:   /          （相对于 Root path，即 /opt/ibmcp）
    Web path:          /          （留空即可）

步骤 4：选中 euler-ibmcp → 点工具栏 "Use as Default" 按钮
        （设为默认服务器，自动上传只会传到默认服务器）

步骤 5：Options（Ctrl+Alt+S → Build, Execution, Deployment | Deployment | Options
        或菜单 Tools | Deployment | Options）：
    Upload changed files automatically to the default server: Always
    Delete remote files when local are deleted: ✅（保持镜像一致）
    Skip external changes: ✅（排除 VCS/脚本产生的外部变更，避免误传）
```

#### 5.1.3 首次手动同步（详细操作步骤）

§5.1.2 配好 Deployment 后，需要做一次**全量上传**，把 Windows 的整个项目首次同步到欧拉 `/opt/ibmcp`。之后日常保存文件即自动
upload，无需重复此操作。

> **前提**：① §5.1.2 Deployment 已配好并能 Test Connection 通过；② 欧拉侧 `/opt/ibmcp` 目录已存在（`bootstrap.sh` 会创建，或手动
> `sudo mkdir -p /opt/ibmcp && sudo chown meng:meng /opt/ibmcp`）。

**操作步骤（PyCharm 2026.3 界面）**：

```
步骤 1：打开 Project 工具窗口（Alt+1）
        → 在项目树中选中项目根节点 "02-IBMCP"
        → 右键

步骤 2：右键菜单 → Deployment | Upload to
        → 从服务器列表中选择 "euler-ibmcp"

        ※ 如果 euler-ibmcp 已设为默认服务器（§5.1.2 步骤 4），
           菜单会直接显示 "Upload to euler-ibmcp"，点它即可

步骤 3：PyCharm 底部弹出 "File Transfer" 工具窗口
        → 显示传输文件列表，逐行实时刷新：
          Uploading backend/app/main.py ... done
          Uploading backend/app/core/config.py ... done
          Uploading collector-cpp/src/collector.cpp ... done
          ...
        → 底部状态栏显示总进度条
        → 全量上传可能持续 1~3 分钟（取决于项目大小和网络）

步骤 4：上传完成后，File Transfer 窗口顶部显示统计：
        "N files uploaded (M successes, K failures)"
        → 如果有失败项（通常是权限问题）：
          在 File Transfer 窗口中右键失败条目 → Retry Upload
        → 或选中失败条目 → 右键 → "Upload to euler-ibmcp" 重试
```

**验证文件已到达欧拉侧**：

```bash
# 在欧拉虚拟机上执行（SSH 或 VMware 控制台）
ssh meng@192.168.80.134

# 检查项目根目录
ls /opt/ibmcp/
# 应看到：backend/  frontend/  collector-cpp/  python/  deployments/  packaging/  pyproject.toml ...

# 检查关键文件
ls /opt/ibmcp/backend/app/main.py          # FastAPI 入口
ls /opt/ibmcp/collector-cpp/CMakeLists.txt # C++ 构建脚本
ls /opt/ibmcp/deployments/openeuler/docker-compose.yml
ls /opt/ibmcp/deployments/openeuler/bootstrap.sh

# 确认排除项没有传上去（不应该存在）
ls /opt/ibmcp/venv/              # 应不存在（Windows venv 不该传）
ls /opt/ibmcp/frontend/node_modules/  # 应不存在（欧拉侧单独 npm install）
ls /opt/ibmcp/backend/app/__pycache__/ # 应不存在
```

**确认自动同步已生效**：

```
步骤 1：在 PyCharm 中随便打开一个文件，比如 backend/app/core/config.py
步骤 2：加一行注释（如 # sync test）→ Ctrl+S 保存
步骤 3：观察 File Transfer 工具窗口
        → 应出现一条新的传输记录：config.py → euler-ibmcp
        → 或底部状态栏短暂闪现上传提示

步骤 4：欧拉侧验证
        grep "sync test" /opt/ibmcp/backend/app/core/config.py
        → 能匹配到说明自动同步正常工作

步骤 5：删掉刚才的测试注释 → Ctrl+S → 确认再次自动上传
```

> **常见问题**：
> - **右键菜单没有 "Deployment" 项**：确认 PyCharm Professional 版（Community 版无 Deployment 功能）；确认 FTP/SFTP/WebDAV Connectivity 插件已启用（`Ctrl+Alt+S → Plugins → Installed → 搜索 SFTP`）；确认已配置至少一个
>   SFTP 连接（§5.1.2）
> - **上传失败 "Permission denied"**：欧拉侧 `/opt/ibmcp` 目录 owner 不是 `meng`，执行
>   `sudo chown -R meng:meng /opt/ibmcp`
> - **上传失败 "Could not connect"**：检查 SSH 免密是否配好（09 号 §2.4）；`ssh meng@192.168.80.134` 能否手动连通
> - **保存后没有自动上传**：`Tools | Deployment | Options` → "Upload changed files automatically to the default server" 确认设为
>   "Always"；确认 euler-ibmcp 已设为默认服务器（`Ctrl+Alt+S → Build, Execution, Deployment | Deployment` → 选中 euler-ibmcp → 点 "Use as Default"）
> - **传了不该传的文件**：见下方排除项，配好排除后重新全量上传一次

> **排除项**（`.idea/deployment.xml` 或 `Ctrl+Alt+S → Build, Execution, Deployment | Deployment | Options → Excluded Paths`）：
> ```
> Ctrl+Alt+S → Build, Execution, Deployment | Deployment | Options → Excluded Paths → +
>     Local path: E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\.venv
>     Local path: E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\venv
>     Local path: E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\frontend\node_modules
>     Local path: E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\frontend\dist
>     Local path: E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\build
> ```
> - `.venv/`、`venv/`（Windows 的 venv 不要传到欧拉）
> - `node_modules/`（前端依赖在欧拉侧单独 npm install）
> - `__pycache__/`、`.ruff_cache/`
> - `frontend/dist/`、`ibmcp.egg-info/`
> - `build/`（C++ 编译临时）
> - `.git/`（git 历史不需要同步，git 操作只在 Windows 做）

> **手动同步 vs 自动同步的关系**：首次全量上传是一次性的"铺底"操作，把所有源文件一次性推过去。之后的自动同步只传你保存时改动的那个文件（增量），非常快。如果某次发现欧拉侧代码与
> Windows 不一致（比如别人改了欧拉侧、或同步中断），随时可以再次右键项目根 → Upload to euler-ibmcp 做一次全量同步。

#### 5.1.4 PyCharm 内置数据库工具（连 Docker PG/Redis）

PyCharm 右侧边栏 Database → + → Data Source：

| 服务                     | Host             | Port   | DB      | User    | Password | 备注                        |
|--------------------------|------------------|--------|---------|---------|----------|-----------------------------|
| PostgreSQL               | `192.168.80.134` | `5432` | `ibmcp` | `ibmcp` | `ibmcp`  | Docker 容器，端口映射到宿主 |
| Redis（装 Redis 插件后） | `192.168.80.134` | `6379` | —       | —       | —        | Docker 容器，端口映射到宿主 |

> **两种连法**：① 直连 `192.168.80.134:5432`（docker-compose.yml 已映射 `5432:5432`）；② 用 SSH 隧道
> `127.0.0.1:5432 → euler:5432`（更安全，见 §6）。

### 5.2 CLion（C++ 扩展远程开发 + gdb）

**前置条件**：Windows 安装 CLion 2026.3（Remote Host 是 Ultimate 版特性）

#### 5.2.1 配置 Remote Host Toolchain

```
Ctrl+Alt+S → Build, Execution, Deployment | Toolchains → + → Remote Host
        Credentials → 选已配好的 SSH 配置 "euler-ibmcp"
        CMake executable: /usr/bin/cmake
        C++ Compiler:     /usr/bin/g++
        Debugger:         /usr/bin/gdb
→ 点 "Test Connection" → CLion 自动在欧拉上装 C++ Remote Debugger
```

#### 5.2.2 配置 Deployment + CMake Profile

```
Ctrl+Alt+S → Build, Execution, Deployment | Deployment → + → SFTP → "euler-ibmcp"
        （可复用 PyCharm 同名的 SFTP 配置，或新建一个指向 collector-cpp 子目录）
        Mappings → Local path:        E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\collector-cpp
                   Deployment path:    /opt/ibmcp/collector-cpp
        Options → Upload changed files automatically to the default server: Always
        关键：勾选 "Never overwrite local files from deployment server" ← 禁止反向下拉

Ctrl+Alt+S → Build, Execution, Deployment | CMake → + → Debug-Remote
        Toolchain: Remote Host
        Build directory: /opt/ibmcp/build/cmake-build-debug
        Generator: Unix Makefiles
        CMake options: -DCMAKE_BUILD_TYPE=Debug -DIBMCP_HAS_MODBUS=ON ...

再建一个 Release-Remote（不带 Debug 符号，日常用）
```

> **CLion CMake Profile vs pip install 的关系**：CLion 的 CMake Profile 只做**代码索引、跳转、静态分析**和**gdb
> 调试符号加载**。正式编译 + 安装扩展到 venv 的**唯一路径**是 `pip install ./python ./backend`（见 §4.1）。两者共享同一份
> `collector-cpp/CMakeLists.txt`。

### 5.3 目录映射与双 IDE 同步纪律

```
Windows 本地路径（源头）                    ←→  欧拉路径（自动同步镜像）
E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\
  ├─ backend\app\                           ←→  /opt/ibmcp/backend/app/
  ├─ frontend\                              ←→  /opt/ibmcp/frontend/
  ├─ collector-cpp\                         ←→  /opt/ibmcp/collector-cpp/
  ├─ python\                                ←→  /opt/ibmcp/python/
  ├─ deployments\                           ←→  /opt/ibmcp/deployments/
  └─ pyproject.toml                         ←→  /opt/ibmcp/pyproject.toml
```

| #  | 纪律                                                            | 理由                                                   |
|----|-----------------------------------------------------------------|--------------------------------------------------------|
| D1 | **代码只在 Windows 编辑**（PyCharm/CLion）                      | Windows 是源头，欧拉是镜像                             |
| D2 | **保存即自动 upload**（PyCharm Deployment + CLion SFTP）        | 保持欧拉镜像与 Windows 一致                            |
| D3 | **不在欧拉侧手动改代码**                                        | 会被下次同步覆盖；如需临时改，先在 Windows 改再 upload |
| D4 | **git 只在 Windows 执行**                                       | 避免两边 git 状态不一致；欧拉侧不 init git             |
| D5 | `venv/`、`node_modules/`、`__pycache__/`、`build/` **绝不同步** | 机器绑定的二进制和路径                                 |
| D6 | CLion Deployment 设为 **Never download from server**            | 防止欧拉侧 CMakeCache 反向下拉污染 Windows 索引        |

---

## 六、SSH 隧道与端口映射

### 6.1 端口总览

| 端口 | 服务                  | 跑在哪                               | 谁在用                                 |
|------|-----------------------|--------------------------------------|----------------------------------------|
| 22   | SSH 本身              | 欧拉宿主                             | PyCharm 远程解释器 / CLion Remote Host |
| 5432 | PostgreSQL 15         | **Docker 容器** `ibmcp-postgres`     | PyCharm Database / DBeaver             |
| 6379 | Redis 7               | **Docker 容器** `ibmcp-redis`        | PyCharm Database / Redis Insight       |
| 1880 | Node-RED              | **Docker 容器** `ibmcp-nodered`      | 浏览器（PLC 模拟器/协议探针）          |
| 8000 | FastAPI（HTTP + WS）  | **欧拉裸机** venv                    | Chrome / 前端 Vite proxy / Nginx 反代  |
| 80   | Nginx 反代 + 前端静态 | **欧拉裸机** nginx                   | Chrome（生产/部署用）                  |
| 5678 | debugpy               | **欧拉裸机**                         | PyCharm Debugger                       |
| 1234 | gdbserver             | **欧拉裸机**                         | CLion Remote Debugger                  |
| 5173 | 前端 Vite dev server  | Windows 本地（开发期）/ 欧拉（备选） | Chrome                                 |

**核心**：PG/Redis/Node-RED 跑在 Docker 容器里（端口映射到宿主），FastAPI/C++/Nginx 裸机跑在宿主上。Windows 侧通过 SSH
隧道或直连访问。

### 6.2 隧道建立（两选一）

#### 方式 A（推荐）：写在 ~/.ssh/config 一劳永逸

```
# $env:USERPROFILE\.ssh\config
Host euler-ibmcp
    HostName 192.168.80.134
    User meng
    Port 22
    IdentityFile ~/.ssh/ibmcp_euler
    ServerAliveInterval 30

    LocalForward 5432 localhost:5432    # PG (Docker 容器，映射到宿主)
    LocalForward 6379 localhost:6379    # Redis (Docker 容器，映射到宿主)
    LocalForward 1880 localhost:1880    # Node-RED (Docker 容器)
    LocalForward 8000 localhost:8000    # FastAPI (裸机)
    LocalForward 5678  localhost:5678   # debugpy (裸机)
    LocalForward 1234  localhost:1234   # gdbserver (裸机)
```

#### 方式 B：PyCharm / CLion 自动转发

PyCharm 远程解释器连接时自动转发常用端口。自定义端口（5678/1234）在 IDE 的 Forwarded Ports 面板里手动加一次。

### 6.3 隧道验证

```powershell
# Windows PowerShell 一键验证
foreach ($p in 5432, 6379, 1880, 8000, 5678, 1234) {
    $ok = Test-NetConnection -ComputerName 127.0.0.1 -Port $p -WarningAction SilentlyContinue
    Write-Host "Port $p : $($ok.TcpTestSucceeded)"
}

# 或直连验证（不用隧道）
foreach ($p in 5432, 6379, 1880, 8000) {
    $ok = Test-NetConnection -ComputerName 192.168.80.134 -Port $p -WarningAction SilentlyContinue
    Write-Host "Port $p : $($ok.TcpTestSucceeded)"
}

# 浏览器直接访问：
# http://127.0.0.1:8000/api/v1/docs     → FastAPI OpenAPI（隧道）
# http://192.168.80.134:8000/api/v1/docs → FastAPI OpenAPI（直连）
# http://192.168.80.134:1880            → Node-RED（Docker 容器）
```

---

## 七、调试链路详解

### 7.1 Python 代码：PyCharm 远程调试

#### 7.1.1 创建 Run/Debug Configuration

```
Run → Edit Configurations → + → Python

# Profile A：真实扩展（native）+ 现场设备/模拟器
Name: FastAPI (dev, native)
Module name: uvicorn
Parameters:
  app.main:app --host 0.0.0.0 --port 8000 --reload
Environment variables:
  COLLECTOR_BACKEND=native
  PG_HOST=127.0.0.1
  PG_PORT=5432
  PG_USER=ibmcp
  PG_PASSWORD=ibmcp
  PG_DATABASE=ibmcp
  REDIS_URL=redis://127.0.0.1:6379/0
Working directory: /opt/ibmcp/backend
Python interpreter: SSH: euler-ibmcp → /opt/ibmcp/venv/bin/python3.11

# Profile B：stub 模式（无硬件时跑全业务链，Python/前端/DB 全绿）
Name: FastAPI (stub, no hardware)
Module name: uvicorn
Parameters:
  app.main:app --host 0.0.0.0 --port 8000
Environment variables:
  COLLECTOR_BACKEND=stub
  PG_HOST=127.0.0.1
  PG_PORT=5432
  PG_USER=ibmcp
  PG_PASSWORD=ibmcp
  PG_DATABASE=ibmcp
  REDIS_URL=redis://127.0.0.1:6379/0
```

> **关键对齐点**：
> - **启动命令是 `app.main:app`**（不是 `app.main:create_app --factory`）。`backend/app/main.py` 同时定义了 `create_app()`
>   函数和模块级 `app = create_app()`，systemd service 和 Dockerfile 都用 `app.main:app`。
> - **`COLLECTOR_BACKEND` 取值是 `native` / `stub`**（不是 `ffi`）。见 §4.4。
> - **PG 连接用 `PG_HOST` 等独立变量**，不是 `DATABASE_URL`。`config.py` 的 `pg_dsn` 属性会拼成
>   `postgresql://ibmcp:ibmcp@127.0.0.1:5432/ibmcp`。
> - **Working directory 是 `/opt/ibmcp/backend`**（不是 `/opt/ibmcp`），因为 `app.main` 的 import 路径是
>   `backend.app.main`。

> **`COLLECTOR_BACKEND` 的选择逻辑与 stub 实现细节**（stub 如何复刻 CollectorApi 全部方法、`ffi.py` 的 `get_collector()`
> 如何分发、stub.py 与 protocol.py 的 Protocol
> 统一接口）全部以 [05-绑定契约 stub/native 章节](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md)
> 为权威，本文档只保留开发环境的 PyCharm Run/Debug 配置步骤。

#### 7.1.2 启动调试

```
PyCharm → Debug（虫子图标）→ PyCharm 自动：
  ├─ 通过 SSH 在欧拉 venv 里启动 uvicorn（端口 8000）
  ├─ 注入 debugpy 监听 5678
  ├─ 把断点、变量、单步事件通过 SSH 传回 Windows
  └─ 在 PyCharm 里显示完整调试面板
```

> **数据库连接串里的 `localhost`/`127.0.0.1`**：FastAPI 跑在欧拉裸机上，PG/Redis 跑在 Docker 容器里但端口映射到宿主的
> `127.0.0.1:5432` / `127.0.0.1:6379`。所以 FastAPI 用 `PG_HOST=127.0.0.1` 连到的就是容器里的 PG，无需特殊网络配置。

### 7.2 C++ 扩展：CLion gdbserver 远程附着

#### 7.2.1 准备：放开 ptrace

```bash
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
echo "kernel.yama.ptrace_scope = 0" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

#### 7.2.2 附着步骤

```bash
# 欧拉侧 Terminal（PyCharm 或 CLion 里开都行）：
cd /opt/ibmcp && source venv/bin/activate

# 先跑 uvicorn（FastAPI 必须在宿主裸机上跑）
uvicorn app.main:app --host 0.0.0.0 --port 8000 &
PID=$!

# gdbserver 附着
gdbserver :1234 --attach $PID
```

```
CLion 侧：
Run → Attach to Process → Remote Host → euler-ibmcp
→ Enter manually → Host: 127.0.0.1, Port: 1234
 CLion 连上 → 自动加载 CMake Profile 符号表
 在 collector-cpp/src/*.cpp 里设断点
 触发采集 → CLion 断点接住
```

#### 7.2.3 调试符号说明

日常 `pip install ./python ./backend` 产出的是 Release 二进制（无 `-g` 符号），CLion gdb 断点会报 "No executable code
found"。需要 C++ 单步时切换 Debug 编译：

```bash
cd /opt/ibmcp
source venv/bin/activate

# Debug 编译（带 -g 符号）
CMAKE_BUILD_TYPE=Debug pip install ./python ./backend --no-build-isolation
```

### 7.3 C++ 与 Python 跨语言混合断点

```
步骤 1：PyCharm Debug 启动 uvicorn（端口 8000，Profile: FastAPI (dev, native)）
        → 在 backend/app/services/plan_compiler.py 某行设断点 → 点 Debug → 停住

步骤 2：CLion Attach 到同一个 uvicorn 进程（Run → Attach to Process → Remote Host → euler-ibmcp → Host 127.0.0.1, Port 1234）
        → 在 collector-cpp/src/ 设 C++ 断点

步骤 3：PyCharm 里点 Continue
        → Python 持有 GIL 调 C++ → CLion 断点自动接住 → 单步 → 执行完回到 Python

步骤 4：调试完毕 → 先停 gdbserver，再停 PyCharm Debug Session
```

> **GIL 与死锁规则**（什么时候必须 `py::gil_scoped_release`、`stop()` 为什么会死锁、DataStore 为什么不能包
> GIL）全部在 [05-绑定契约 GIL 规则表](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md)
> 中有明确定义，"PyCharm Continue 后卡死不动" 是 `stop()` 死锁的典型表现。

---

## 八、前端调试与部署

### 8.1 开发期：Vite 在 Windows 本地跑 + 直连欧拉 FastAPI

```
Windows PowerShell：
cd E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\frontend
npm install
npm run dev        ← Vite HMR 完全本地，改代码毫秒级热更
# 浏览器访问 http://localhost:5173
```

`frontend/vite.config.ts` 的 proxy 配置（已与代码对齐）：

```typescript
server: {
    host: '0.0.0.0',
        port
:
    5173,              // ← 不是 3000
        strictPort
:
    true,
        proxy
:
    {
        '/api'
    :
        {
            target: 'http://192.168.80.134:8000',   // ← 直连欧拉裸机 FastAPI
                changeOrigin
        :
            true,
        }
    ,
        '/ws'
    :
        {
            target: 'ws://192.168.80.134:8000',
                ws
        :
            true,
                changeOrigin
        :
            true,
        }
    ,
    }
,
}
,
```

数据流：

```
浏览器 → Vite proxy → 192.168.80.134:8000 → 欧拉裸机 FastAPI
```

### 8.2 部署期：前端构建并部署到 Nginx 静态目录

开发完成后，将前端构建产物部署到 Nginx 的静态文件目录（`/opt/ibmcp/frontend-dist`，由 `packaging/nginx/ibmcp.conf` 的
`root` 指定）：

```bash
# —— 在欧拉虚拟机上执行 ——
cd /opt/ibmcp/frontend

# 安装前端依赖（首次）
npm install

# 构建（产出 dist/ 目录）
npm run build

# 部署到 Nginx 静态目录
mkdir -p /opt/ibmcp/frontend-dist
cp -r dist/* /opt/ibmcp/frontend-dist/

# Nginx 配置已在 bootstrap.sh 中部署到 /etc/nginx/conf.d/ibmcp.conf
# 重载 Nginx（无中断）
sudo nginx -t && sudo nginx -s reload
```

部署后的数据流：

```
浏览器 → http://192.168.80.134           → Nginx 返回前端静态文件（index.html）
浏览器 → http://192.168.80.134/api/v1/*   → Nginx 反代 → 裸机 FastAPI :8000
浏览器 → http://192.168.80.134/api/v1/ws/* → Nginx 反代 WS → 裸机 FastAPI :8000
```

`packaging/nginx/ibmcp.conf` 配置要点：

```nginx
server {
    listen 80;
    server_name _;
    root /opt/ibmcp/frontend-dist;     # ← 前端静态文件目录
    index index.html;

    location /api/v1/ws/collect {      # ← WebSocket 反代
        proxy_pass http://127.0.0.1:8000/api/v1/ws/collect;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 3600s;       # ← WS 长连接超时
    }

    location /api/ {                    # ← HTTP API 反代
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    location / {                        # ← 前端 SPA fallback
        try_files $uri $uri/ /index.html;
    }
}
```

> **前端更新流程**：每次前端代码改完需要部署时，在欧拉侧重新 `npm run build` + `cp dist/* /opt/ibmcp/frontend-dist/` +
> `nginx -s reload`。开发期用 Vite dev server（§8.1）无需频繁部署。

### 8.3 备选：Vite dev server 也跑在欧拉上

```bash
cd /opt/ibmcp/frontend && npm install && npm run dev -- --host 0.0.0.0 --port 5173
# SSH 隧道 5173 → 浏览器 http://127.0.0.1:5173
```

---

## 九、开发工作流

### 9.1 每天启动 checklist

```
□ VMware → 启动 openEuler 虚拟机 → 确认 IP 192.168.80.134
□ Docker 容器：docker compose -f /opt/ibmcp/deployments/openeuler/docker-compose.yml ps
    → ibmcp-postgres / ibmcp-redis / ibmcp-nodered 全部 Up
□ 裸机服务：systemctl status nginx → active
□ 欧拉侧 venv：source /opt/ibmcp/venv/bin/activate → python --version
□ PyCharm → 打开 02-IBMCP → 确认远程解释器已连（状态栏显示 SSH: euler-ibmcp）
□ PyCharm → 首次同步确认（Deployment → Upload to euler-ibmcp，或确认自动同步开启）
□ CLion → Open Recent → Remote Host 自动连上
□ 隧道验证（Test-NetConnection 5432/6379/1880/8000）
□ 浏览器：
    http://192.168.80.134         → 前端（Nginx 静态，已部署时）
    http://192.168.80.134/api/v1/docs → FastAPI OpenAPI（Nginx 反代）
    http://192.168.80.134:8000/api/v1/docs → FastAPI OpenAPI（直连）
    http://localhost:5173          → 前端 Vite dev（开发期）
    http://192.168.80.134:1880     → Node-RED（Docker）
□ PyCharm Database → ibmcp → 表结构正常
```

### 9.2 Python 开发循环

```
PyCharm（Windows 本地索引）
    │ 编辑 backend/app/services/plan_compiler.py（本地毫秒级索引）
    │ Ctrl+S → PyCharm Deployment 自动 upload 到 /opt/ibmcp/backend/
    │ 点 Debug → FastAPI (dev, native) → 欧拉侧 uvicorn --reload 自动重启
    │ PyCharm HTTP Client（.http 文件）直接发 API 请求
    │ 浏览器 http://localhost:5173 看前端实时监控
```

### 9.3 C++ 开发循环

```
CLion Remote Host（Windows 本地索引）
    │ 编辑 collector-cpp/src/scheduler/io_pool.cpp
    │ Ctrl+S → CLion SFTP upload 到 /opt/ibmcp/collector-cpp/
    │ 欧拉侧 Terminal: cd /opt/ibmcp && pip install ./python ./backend（Release）
    │   或 CMAKE_BUILD_TYPE=Debug pip install ./python ./backend --no-build-isolation（调试）
    │ gdbserver :1234 --attach $(pgrep -f uvicorn)
    │ CLion Attach → 断点 → 单步跟踪
```

### 9.4 联调顺序（**严格按此顺序**）

```
阶段 1：PyCharm + stub（无硬件）
  └─ 前端页面、配置向导、历史查询、控制下发 UI
     PyCharm Debug 选 "FastAPI (stub, 8000)"
     验证整条 Python 业务链跑通

阶段 2：PyCharm + 真扩展 + 无设备
  └─ 验证 pip install ./python ./backend 编译成功、import 正常
     NativeCollector.init() / list_protocols() / health() 返回正确
     空 plan 下发 + apply_plan 返回 0

阶段 3：真扩展 + 模拟器
  └─ 搭 Modbus TCP 模拟器（可用 Python modbus-tk 或独立 docker run nodered）
     验证 Modbus TCP 读写、scale/offset、多帧合并
     搭 MQTT broker → 验证 MQTT 驱动

阶段 4：真扩展 + 真实设备
  └─ 一台台把现场 PLC IP 加到 plan
     逐协议验证 + 压力测试 5000 点位 × 1s

阶段 5：全链路端到端
  └─ 前端配置向导 → 配置下发 → 设备在线 → 实时监控 → 历史查询 → 报警
```

> **阶段 3 不可省**：直连真 PLC 时"读不到"永远先怀疑"是代码问题还是 PLC 配置问题"。模拟器把两者分开，90% 的问题在阶段 3
> 就能排除。

### 9.5 重新编译 C++ 扩展

```bash
cd /opt/ibmcp && source venv/bin/activate

pip install ./python ./backend                                          # Release，日常
rm -rf build/ && pip install ./python ./backend --no-build-isolation    # 完整清理
CMAKE_BUILD_TYPE=Debug pip install ./python ./backend --no-build-isolation  # Debug，CLion 调试用

python -c "import industrial_collector._core as c; print('✅ OK'); print(c.health())"
```

---

## 十、Node-RED 的开发调试定位

Node-RED 已包含在 `docker-compose.yml` 中（容器名 `ibmcp-nodered`，端口 1880），由 `bootstrap.sh` 自动启动。作用：

| # | 用途                                                                    |
|---|-------------------------------------------------------------------------|
| 1 | **PLC 模拟器**：modbus-in/out 节点搭 Modbus TCP 服务器（阶段 3 联调用） |
| 2 | **MQTT broker + 订阅**：快速搭 MQTT 调试环境                            |
| 3 | **协议探针**：TCP Request 节点发自定义 Socket 报文                      |
| 4 | **数据导出**：FastAPI WS → Node-RED → Grafana / 其他系统                |

**常用操作**：

```bash
cd /opt/ibmcp

# 查看状态
docker compose -f deployments/openeuler/docker-compose.yml ps

# 只重启 Node-RED
docker compose -f deployments/openeuler/docker-compose.yml restart nodered

# 停止全部容器（不删数据）
docker compose -f deployments/openeuler/docker-compose.yml down

# 启动全部容器
docker compose -f deployments/openeuler/docker-compose.yml up -d

# 查看 Node-RED 日志
docker logs ibmcp-nodered -f --tail 50

# 浏览器访问
# http://192.168.80.134:1880
```

> **Node-RED 流程持久化**：Node-RED 的流程配置存在 Docker 命名卷 `ibmcp_nodered_data` 中，`docker compose down` 不会丢失。只有
> `docker compose down -v` 才会删除卷。

---

## 十一、常见问题排查速查

### 11.1 SSH / 隧道 / 同步类

| 症状                        | 排查                                                                                                       |
|-----------------------------|------------------------------------------------------------------------------------------------------------|
| PyCharm 远程解释器连不上    | `ping 192.168.80.134` 不通 → VMware 桥接/IP 问题；通 → 检查 `~/.ssh/config` 和 `sshConfigs.xml`            |
| CLion Remote Host 连不上    | 同上；Deployment SFTP 映射路径 `/opt/ibmcp/collector-cpp` 是否存在                                         |
| PyCharm 保存后欧拉侧没更新  | 检查 Deployment → Options → Upload changed files automatically 是否设为 Always；排除路径是否误排了源码目录 |
| 隧道端口不通                | 欧拉侧 `ss -tlnp \| grep 5432`；`ssh -v` 看 LocalForward                                                   |
| 欧拉侧代码与 Windows 不一致 | 右键项目根 → Deployment → Upload to euler-ibmcp 全量同步一次                                               |

### 11.2 Docker 容器类（PG / Redis / Node-RED）

| 症状                                         | 排查                                                                                                              |
|----------------------------------------------|-------------------------------------------------------------------------------------------------------------------|
| 容器没启动                                   | `docker compose -f deployments/openeuler/docker-compose.yml ps`；`docker logs ibmcp-postgres` 看报错              |
| PG 连不上                                    | `docker exec ibmcp-postgres pg_isready -U ibmcp -d ibmcp`；端口映射是否 `5432:5432`；宿主 `ss -tlnp \| grep 5432` |
| PG 认证失败 `password authentication failed` | 确认用户 `ibmcp` 密码 `ibmcp`（`PGPASSWORD=ibmcp psql -U ibmcp -h 127.0.0.1 -d ibmcp`）                           |
| Redis 连不上                                 | `docker exec ibmcp-redis redis-cli ping` 应返回 PONG；端口映射 `6379:6379`                                        |
| Node-RED 访问不了                            | `docker logs ibmcp-nodered --tail 30`；端口映射 `1880:1880`；`curl http://127.0.0.1:1880`                         |
| 容器重启后数据丢失                           | 检查 docker-compose.yml 的 volumes 是否正确；`docker volume ls \| grep ibmcp`                                     |
| Docker 守护进程没跑                          | `systemctl status docker`；`sudo systemctl start docker`                                                          |
| `docker compose` 命令不存在                  | openEuler 24.03 装的是 `docker-compose-plugin`，命令是 `docker compose`（无连字符），不是 `docker-compose`        |
| 需要重置 PG 数据                             | `docker compose down -v` 删除卷 → `docker compose up -d` 重建（⚠️ 会丢全部 PG 数据）                              |

### 11.3 Nginx 类（裸机）

| 症状                                    | 排查                                                                                                                                                     |
|-----------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------|
| Nginx 502 connect() failed 反代 FastAPI | `curl http://127.0.0.1:8000` 在欧拉测 → FastAPI 是否在跑                                                                                                 |
| Nginx 改了配置没生效                    | `sudo nginx -t && sudo nginx -s reload`（不要 systemctl restart，reload 无中断）                                                                         |
| 前端页面 404                            | 确认 `/opt/ibmcp/frontend-dist/index.html` 存在（`npm run build` + `cp dist/*`）；nginx.conf `root` 是否指向 `/opt/ibmcp/frontend-dist`                  |
| 前端 SPA 刷新 404                       | nginx.conf `location /` 必须有 `try_files $uri $uri/ /index.html`                                                                                        |
| WS 反代失败                             | nginx.conf WS location 必须加 `proxy_http_version 1.1` + `proxy_set_header Upgrade $http_upgrade` + `Connection upgrade` + 足够大的 `proxy_read_timeout` |

### 11.4 Python / FastAPI 类

| 症状                                                                 | 排查                                                                                                                           |
|----------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------|
| `ModuleNotFoundError: industrial_collector._core`                    | 欧拉侧 `pip install ./python ./backend` 是否成功；PyCharm 解释器是不是 `/opt/ibmcp/venv/bin/python3.11`                        |
| `RuntimeError: industrial_collector._core 未安装`                    | `COLLECTOR_BACKEND` 设成了 `native` 但没编译扩展 → 改 `stub` 或执行 build.sh                                                   |
| `ImportError: dynamic module does not define module export function` | **Python ABI 不匹配**：扩展用 3.11 编的却被 3.12 import → 确认 venv 用 python3.11                                              |
| PG 连接报错                                                          | 确认 `PG_HOST=127.0.0.1` `PG_USER=ibmcp` `PG_PASSWORD=ibmcp` `PG_DATABASE=ibmcp`（不是 `postgres/device_db`）；PG 容器是否在跑 |
| `ValueError: unknown collector backend: ffi`                         | `COLLECTOR_BACKEND` 只接受 `stub` 或 `native`（不是 `ffi`）                                                                    |

### 11.5 C++ 扩展类

| 症状                                           | 排查                                                                                                           |
|------------------------------------------------|----------------------------------------------------------------------------------------------------------------|
| CLion 断点 "No executable code found"          | 没带 `-g` → `CMAKE_BUILD_TYPE=Debug pip install ./python ./backend --no-build-isolation`                       |
| `ptrace_scope` 报错（gdbserver 无法 attach）   | `echo 0 \| sudo tee /proc/sys/kernel/yama/ptrace_scope`                                                        |
| `libmodbus.so: cannot open shared object file` | `dnf install libmodbus-devel` 或 `echo "/usr/lib64" \| sudo tee /etc/ld.so.conf.d/ibmcp.conf && sudo ldconfig` |
| cmake 报 `CMAKE_CXX_STANDARD 17` 不支持        | gcc 版本太老 → `dnf update gcc gcc-c++`（openEuler 24.03 自带支持 C++17）                                      |

> **GIL 死锁排查**（`gdbserver attach` 后 continue 崩、扩展 shutdown 卡死、Python 持有 GIL 阻塞）→
> 根因与修复全部见 [05-绑定契约 GIL 规则表 / stop() 死锁修复](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md)。

### 11.6 前端类

| 症状                    | 排查                                                                                                        |
|-------------------------|-------------------------------------------------------------------------------------------------------------|
| `/api/v1/...` 404       | Vite proxy target 是否指向欧拉（`192.168.80.134:8000`）；FastAPI 是否在欧拉跑着                             |
| WebSocket 连不上        | Vite proxy `/ws` 加 `ws: true`；FastAPI `/api/v1/ws/collect` 路径正确                                       |
| 前端报 `CORS`           | `vite.config.ts` 里 `server.proxy` 已代理 → 代理模式下浏览器到 Vite 同源，无 CORS                           |
| Vite 端口 5173 被占用   | `vite.config.ts` 设了 `strictPort: true` → 换端口或杀占用进程                                               |
| 部署到 Nginx 后页面空白 | `npm run build` 是否成功；`dist/*` 是否已 `cp` 到 `/opt/ibmcp/frontend-dist/`；nginx.conf `root` 路径是否对 |

---

## 十二、开发期资源消耗参考

### 12.1 欧拉虚拟机各服务资源占用

| 服务                         | 内存           | CPU       | 磁盘            | 备注                                    |
|------------------------------|----------------|-----------|-----------------|-----------------------------------------|
| PostgreSQL 15（Docker 容器） | 200~500 MB     | < 10%     | 按数据量增长    | 卷 `ibmcp_pg_data`                      |
| Redis 7（Docker 容器）       | 10~50 MB       | < 5%      | AOF 几十 MB     | 卷 `ibmcp_redis_data`                   |
| Node-RED（Docker 容器）      | 50~100 MB      | < 3%      | 流程配置几十 MB | 卷 `ibmcp_nodered_data`                 |
| nginx（裸机）                | < 10 MB        | < 1%      | —               | 反代 + 前端静态，空闲极轻               |
| uvicorn + 扩展（裸机 venv）  | 150~300 MB     | < 5%→20%  | —               | 含采集线程池                            |
| Python venv                  | ~500 MB        | —         | —               | pip packages                            |
| Docker 镜像                  | —              | —         | ~1 GB           | postgres:15 + redis:7 + nodered         |
| C++ 编译临时                 | —              | —         | ~2 GB           | `build/` 目录，debug 符号特别占空间     |
| 前端构建产物                 | —              | —         | ~20 MB          | `/opt/ibmcp/frontend-dist/`             |
| PyCharm 辅助文件             | ~200 MB        | < 5%      | ~500 MB         | `~/.cache/JetBrains/`（远程解释器辅助） |
| CLion Remote Debugger        | ~100 MB        | < 5%      | ~500 MB         | Remote GDB Server                       |
| **合计（典型开发态）**       | **1.5~2.5 GB** | **< 15%** | **< 5 GB**      | 采集压力大时内存上浮至 3.5 GB           |

### 12.2 磁盘增长预估

| 项目                 | 初始    | 日均增长       | 备注                                                            |
|----------------------|---------|----------------|-----------------------------------------------------------------|
| PG 数据              | —       | ~50~200 MB     | Docker 卷 `ibmcp_pg_data`，取决于点位数量 × 采样间隔 × 保留策略 |
| Docker 镜像          | ~1 GB   | —              | postgres:15 + redis:7 + nodered                                 |
| Python venv          | ~500 MB | —              | pip packages                                                    |
| C++ 编译临时         | ~2 GB   | 每次编译 +2 GB | `build/` 目录                                                   |
| 前端构建产物         | ~20 MB  | —              | `/opt/ibmcp/frontend-dist/`                                     |
| PyCharm + CLion 缓存 | ~1 GB   | 少量           | `.cache/JetBrains/`                                             |

**建议**：`rm -rf /opt/ibmcp/build/` 每次 Release 编译完清；PG 数据用 `docker exec ibmcp-postgres pg_dump` 定期备份。

---

## 十三、版本与变更记录

| 版本     | 日期       | 变更                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
|----------|------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **v6.2** | 2026-11-20 | **§5.1 全部对齐 PyCharm 2026.3 界面 + §5.1.3 扩写为详细操作指南**：① IDE 版本从 2024.2+ 改为 2026.3；② §5.1.1 SSH 解释器路径改为 `Ctrl+Alt+S → Python | Interpreter → Add Interpreter → On SSH`，补充 Existing/New SSH 配置选择 + Key pair 认证 + 右下角同步气泡（Sync and auto-upload files）；③ §5.1.2 Deployment 路径改为 `Ctrl+Alt+S → Build, Execution, Deployment | Deployment`（或 `Tools | Deployment | Configuration`），SFTP Connection 标签改用 SSH configuration 下拉，新增 "Use as Default" 步骤，Options 路径改为 `Tools | Deployment | Options`；④ §5.1.3 首次同步扩写为详细操作（Project 工具窗口 Alt+1 → 右键 → `Deployment | Upload to` → 选 euler-ibmcp → File Transfer 工具窗口监控进度）；⑤ 新增欧拉侧文件验证命令 + 自动同步验证（加注释 → Ctrl+S → grep）；⑥ Excluded Paths 补充具体路径配置；⑦ 新增常见问题排查（FTP/SFTP/WebDAV Connectivity 插件检查、Use as Default 等）；⑧ §5.2 CLion 同步对齐 2026.3 界面                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| **v6.1** | 2026-11-20 | **混合部署架构调整**：① **PG/Redis/Node-RED 从裸机 dnf 改为 Docker Compose**（新建 `deployments/openeuler/docker-compose.yml`，PG15 + Redis7 + Node-RED，数据卷持久化）；② **`bootstrap.sh` 重写为混合模式**（7 步：SELinux → dnf 裸机工具链 → 装 Docker → docker compose up → Nginx 配置 → uv → ptrace）；③ **前端部署到 Nginx 静态**（新增 §8.2 构建 + 部署到 `/opt/ibmcp/frontend-dist/` 流程 + nginx.conf 配置说明）；④ **Nginx 保持裸机**（托管前端静态 + 反代 FastAPI，不进 Docker）；⑤ **拓扑图更新**（新增 Docker Compose 层，裸机层只留 FastAPI/C++/Nginx）；⑥ **§6 端口表更新**（PG/Redis/Node-RED 标注 Docker 容器，新增 1880 端口）；⑦ **§10 Node-RED 从"可选"改为 docker-compose 内置**；⑧ **§11 排查新增 Docker 容器类 + Nginx 类**；⑨ **§12 资源消耗更新**（新增 Docker 镜像 + 前端构建产物）                                                                                                                                                                                                                                                                                                                                  |
| **v6.0** | 2026-11-20 | **重大架构调整 + 全面对齐实际代码**：① **远程开发模式从 JetBrains Gateway 改为 SSH 远程解释器 + Deployment 自动同步**（Windows 是代码源头，欧拉是自动同步镜像）；② **IP 从 `192.168.1.100` 改为 `192.168.80.134`**；③ **项目路径从 `02-IBMCP_Python` 改为 `02-IBMCP`**，欧拉路径从 `/home/<user>/01-IBMCP` 改为 `/opt/ibmcp`；④ **openEuler 版本从 22.03 LTS SP3 改为 24.03 LTS**（与 Dockerfile 基础镜像对齐）；⑤ **PG 口径从 `postgres/device_db` 无密码改为 `ibmcp/ibmcp/ibmcp`**（与 bootstrap.sh + .env.example + config.py 对齐）；⑥ **C++ 标准从 C++14 改为 C++17**（与 CMakeLists.txt 对齐）；⑦ **`COLLECTOR_BACKEND` 从 `ffi` 改为 `native`**（与 config.py 对齐）；⑧ **启动命令从 `app.main:create_app --factory` 改为 `app.main:app`**（与 systemd/Dockerfile/build.sh 对齐）；⑨ **冒烟测试 API 路径修正**（`from industrial_collector import Collector` → `NativeCollector` + `industrial_collector._core`）；⑩ **前端端口从 3000 改为 5173**（与 vite.config.ts 对齐）；⑪ **新增 §〇 核心问题解答**（欧拉是否需要完整代码）；⑫ 全文交叉引用路径 `02-IBMCP_Python` → `02-IBMCP`；⑬ SSH 开发用户明确为 `meng`，bootstrap 用 `root` |
| v5.1     | 2026-11-20 | Python 版本降级：源码编译从 3.11.9 改为 3.11.6                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| v5.0     | 2026-11-18 | 去重 + 对齐 01-07 文档口径                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| v4.0     | 2026-11-18 | C++ 标准升级（当时写 C++14，实际代码是 C++17）；Nginx 迁移到裸机                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| v3.0     | 2026-11-18 | PG15/Redis/Node-RED 从裸机迁移到 Docker Compose（v6.0 临时改回裸机，v6.1 恢复混合模式）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| v2.0     | 2026-11-18 | IDE 组合统一为 PyCharm + CLion                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| v1.0     | 2026-11-18 | 初版：所有服务裸机安装                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
