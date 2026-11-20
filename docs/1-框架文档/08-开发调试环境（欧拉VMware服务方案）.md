# IBMCP 开发调试环境 · 欧拉 VMware 虚拟机服务方案

> 版本：v8.0　　编写日期：2026-11-20　　配套引用：[01-架构设计](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/01-IBMCP-FastAPI重构架构设计.md) · [02-数据模型](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/02-数据模型与字段契约（FastAPI版）.md) · [04-驱动契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/04-采集扩展驱动与协议契约（FastAPI版）.md) · [05-绑定契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md) · [07-前端契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/07-Vue3前端架构与多端交付契约.md)
>
> **v8.0 变更（Python 环境口径统一，2026-11-20）**：
> - **Windows 侧**：Python 环境统一改用 **uv 管理**，解释器锁定 **3.11.18**（见 `backend/.python-version`），由 `uv sync` 按 `uv.lock` 同步依赖，**不再用** `py -m venv` + `pip install`。一键脚本 `deployments/dev-windows/bootstrap.ps1` 已改为 uv 流程。
> - **欧拉侧**：**不单独安装 Python、不装 uv**，用**系统自带的 `python3.11`**（openEuler 24.03 自带，实测 3.11.6）建 venv + pip 装依赖。`bootstrap.sh` 的 dnf 清单收紧为 **仅 nginx + docker + C++ 构建工具链**（gcc/g++/cmake/make），不再装 `python3`/`python3-devel`/`python3-pip`/`postgresql-devel`。
> - 编译 pybind11 扩展必需的 `Python.h`：欧拉系统自带 python 不含 `-devel`，由 `build.sh` 在编译时**按需补装 `python3.11-devel`**（仅头文件，属 C++ 构建工具链范畴，不是装解释器）。
> - 3.11.6（欧拉）与 3.11.18（Windows）同属 CPython 3.11，pybind11 ABI 兼容（只要求 major.minor 一致），互不影响。
>
> **本文档定位**：给出一套**开箱即用**的开发调试环境方案。核心原则 —— **Python 用 Windows 本地解释器跑，C++ 用欧拉环境编和跑，
> 数据服务（PG/Redis/Node-RED）跑在欧拉 Docker 里，最终整套交付部署到欧拉**。Windows 11 承担"编辑 + Python 运行 + 前端 dev"，
> 欧拉承担"C++ 编译 + 数据服务 + 生产运行"。
>
> **服务部署架构（混合模式）**：
>
> | 部署方式           | 服务                                                                                                  | 原因                                                                                                                     |
> |--------------------|-------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------|
> | **Docker Compose** | PostgreSQL 15 · Redis 7 · Node-RED                                                                    | 纯服务容器，无编译依赖，容器化最省心；数据卷持久化                                                                       |
> | **裸机 dnf**       | C++ 扩展（`_core.so`） · Nginx · gcc-c++ · cmake · libmodbus/open62541                                 | pybind11 扩展**必须在欧拉上用宿主 Python 编译**（ABI/glibc/驱动库三重约束）；Nginx 需要托管前端静态文件 + 反代 FastAPI |
> | **`pip install .`** | Python 依赖（fastapi/uvicorn/asyncpg/...)                                                             | 与 `_core.so` 同源的纯 Python 包，随扩展一起装进欧拉 venv，供**生产运行**用                                               |
>
> **开发期解释器分工（核心设计）**：
>
> | 开发对象           | 解释器 / 编译器位置              | `COLLECTOR_BACKEND` | 说明                                                                                          |
> |--------------------|----------------------------------|---------------------|-----------------------------------------------------------------------------------------------|
> | **Python 业务代码** | **Windows 本地**（**uv 管理 Python 3.11.18** + `.venv`） | `stub`              | 本地毫秒级索引、断点、热重载；连欧拉的 PG/Redis 容器；**不需要欧拉侧的 Python 环境** |
> | **C++ 采集扩展**   | **欧拉**（gcc-c++ / cmake / 系统自带 python3.11 建的 venv） | `native`            | C++ 在 Windows 编辑（CLion 本地索引），编译和运行都在欧拉；调试靠 gdbserver 远程附着  |
>
> **一句话概括**：**Python 用 Windows 解释器跑，C++ 用欧拉环境编和跑，数据服务跑在欧拉 Docker 里，最终交付整套落到欧拉。**
>
> **远程开发模式：PyCharm 本地解释器 + CLion Remote Host**（不是 SSH 远程解释器，也不是 JetBrains Gateway）。代码源头在
> Windows，欧拉侧只有 C++ 编译所需的源码副本。

---

## 〇、核心问题：欧拉要不要完整 Python 环境？

**不要。开发期 Python 完全在 Windows 跑。**

这是本方案最关键的一条设计，直接决定了两侧要装什么：

| 问题                                   | 答案                                                          |
|----------------------------------------|---------------------------------------------------------------|
| 开发期 Python 解释器在哪？             | **Windows 本地**（**uv 管理 Python 3.11.18** + `E:\...\02-IBMCP\.venv`）      |
| 开发期 C++ 编译器在哪？                | **欧拉**（gcc-c++ / cmake / libmodbus-devel）                  |
| 开发期 PG / Redis / Node-RED 在哪？    | **欧拉 Docker**（`docker compose up -d`）                      |
| 开发期 C++ 扩展在哪跑？                | **欧拉**（编译产出 `_core.so`，被欧拉的 venv import）          |
| 开发期 Python 代码在哪跑？             | **Windows**（`COLLECTOR_BACKEND=stub`，纯 Python 无需 `.so`）  |
| 最终部署时全部在哪？                   | **欧拉**（Python venv + `_core.so` + Nginx + Docker，全套）    |

**为什么 Python 可以在 Windows 跑**：`backend/` 是纯 Python 代码，与平台无关；唯一与平台绑定的是 C++ 扩展
`industrial_collector._core`，而它由 `COLLECTOR_BACKEND` 开关隔离。设成 `stub` 时加载
`app/core/collector/stub.py`（纯 Python 实现，完全复刻 `CollectorApi` 接口），不做任何 `import _core`。因此
**Windows 上跑 stub 能覆盖 100% 的业务逻辑**（配置向导、历史查询、控制下发、WebSocket 推送、报警）。

**为什么 C++ 必须在欧拉编译**：三条硬约束叠加，缺一不可——

```
① ABI 约束      pybind11 扩展的 .so 必须与 import 它的 Python 完全同版本（3.11.x）
                 → 用欧拉 venv 的 3.11 编译，就不能在 Windows 的 3.11 import（.so 是 ELF，Windows 是 PE）

② glibc 约束    .so 链接欧拉的 glibc / libstdc++，依赖 ELF 动态链接器与 Linux 系统调用
                 → Windows 根本无法加载 Linux 的 .so

③ 驱动约束      libmodbus.so / libopen62541.so / libmosquitto.so 只有 Linux 版
                 → Windows 上连编译器都找不到这些库的头文件
```

**为什么 C++ 代码可以在 Windows 写**：CLion 通过 Remote Host 模式把欧拉作为构建/运行端，Windows 只做编辑和
索引。CMake 配置、编译、gdb 全部在欧拉执行，Windows 侧只上传源码。

### 0.1 两侧职责一览（开发期）

| 能力                     | Windows 11                 | openEuler 虚拟机              |
|--------------------------|----------------------------|-------------------------------|
| Python 解释器            | ✅ **主力**（uv 管理 3.11.18） | ⚠️ 用**系统自带** python3.11（不单独装），仅作 C++ 扩展的 ABI 宿主 |
| Python 依赖              | ✅ `uv sync` 装全套（按 uv.lock） | ✅ 欧拉 venv + pip（供 native / 生产用） |
| FastAPI 开发运行         | ✅ `stub` 模式，本地跑     | ✅ 仅联调 `native` 时跑       |
| C++ 编译器 / CMake       | ❌ 不装                    | ✅ **主力**                   |
| `_core.so` 编译          | ❌ 不可能                  | ✅ **唯一产地**               |
| PostgreSQL / Redis       | ❌ 不装（连欧拉）          | ✅ Docker 容器                |
| Node-RED                 | ❌ 不装                    | ✅ Docker 容器                |
| Nginx                    | ❌ 不装                    | ✅ 裸机（部署期用）           |
| 前端 Vite dev            | ✅ **主力**（HMR 本地）    | ⚠️ 备选                       |
| 前端 `npm run build`     | ✅ **唯一构建地**（产出 `dist/`） | ❌ 不装 Node，只托管 `dist/` 静态产物 |
| gdbserver / CLion 调试   | ✅ 客户端                  | ✅ 服务端                     |
| git                      | ✅ 唯一源头                | ❌ 不 init                     |

> **前端部署口径**：欧拉**不安装 Node.js / npm、不执行 `npm run build`**；`bootstrap.sh` 的 dnf 清单**不含 nodejs**。前端只在 Windows 构建，`dist/` 静态产物经 scp 上传到欧拉 Nginx 目录（详见 §8.2）。

> **欧拉侧 venv 的真正用途**：不是给 PyCharm 当远程解释器，而是**给 `_core.so` 当 ABI 宿主**——编译时 `-DPython_EXECUTABLE`
> 指向它，`cmake --install` 把 `.so` 装进它的 `site-packages`，冒烟测试和 `native` 联调都用它 import。这个 venv 同时就是
> **生产运行的 venv**。

### 0.2 从开发到部署的一句话数据流

```
【开发期】Windows：改 Python → 本地 stub 跑（8000/5173 都在本地）→ 读写欧拉 Docker 的 PG/Redis
                   改 C++   → CLion 自动上传 → 欧拉编 _core.so → 欧拉 native 联调（192.168.80.134:8000）
                   改前端   → Windows Vite HMR → 代理到 127.0.0.1:8000（stub）或 192.168.80.134:8000（native）

【部署期】Windows：npm run build → 把 frontend\dist\* 上传到欧拉 /home/meng/ibmcp/frontend-dist/
          欧拉：bash build.sh（venv + CMake + .so + pip install .）
              → systemctl start ibmcp-api + nginx
              → docker compose up -d（PG/Redis/Node-RED）
              → backend/.env 改回 PG_HOST=127.0.0.1 / COLLECTOR_BACKEND=native
```

---

## 一、整体架构与职责划分

### 1.1 开发侧拓扑

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Windows 11 开发主机（代码源头 + Python 解释器 + 前端 dev server）        │
│                                                                         │
│  E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\                              │
│  ├─ backend/app/          ← PyCharm 编辑 + 本地 Python 3.11 跑（stub）   │
│  ├─ frontend/             ← PyCharm 编辑 + 本地 Vite dev（HMR）          │
│  ├─ collector-cpp/        ← CLion 编辑（本地索引，编译在欧拉）           │
│  ├─ python/               ← industrial_collector 包（源码）             │
│  ├─ deployments/openeuler/ ← bootstrap.sh + build.sh + docker-compose    │
│  ├─ .venv/                ← Windows 本地虚拟环境（⚠️ 绝不上传）          │
│  └─ pyproject.toml                                                      │
│                                                                         │
│  ┌──────────────────┐  ┌──────────────┐  ┌───────────────────────────┐  │
│  │ PyCharm Prof     │  │ CLion        │  │ Chrome / DBeaver          │  │
│  │ (本地解释器      │  │ (Remote Host │  │ (直连 192.168.80.134)     │  │
│  │  + 前端 + DB)    │  │  上传源码)   │  │                           │  │
│  └────────┬─────────┘  └──────┬───────┘  └──────────────┬────────────┘  │
│           │ TCP 5432/6379/1880│ SFTP 上传 + gdbserver 1234│ HTTP 8000     │
│           │ (直连，无需隧道)  │                          │               │
│           ▼                   ▼                          ▼               │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ VMware 虚拟交换机（桥接模式）
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  openEuler 24.03 LTS 虚拟机（IP: 192.168.80.134，桥接到 Windows 网段）   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │ Docker Compose 层（数据服务，docker compose up -d）              │    │
│  │  ├─ ibmcp-postgres (PG15)   :5432  ibmcp/ibmcp/ibmcp             │    │
│  │  │   └─ 卷 ibmcp_pg_data → /var/lib/postgresql/data             │    │
│  │  ├─ ibmcp-redis (Redis 7)   :6379  appendonly AOF                │    │
│  │  │   └─ 卷 ibmcp_redis_data → /data                              │    │
│  │  └─ ibmcp-nodered (Node-RED) :1880  PLC 模拟器/协议探针           │    │
│  │      └─ 卷 ibmcp_nodered_data → /data                            │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │ C++ 编译/运行层（开发期）                                        │    │
│  │  ├─ gcc-c++ / cmake（C++17）                                     │    │
│  │  ├─ libmodbus-devel / open62541-devel / mosquitto-devel          │    │
│  │  ├─ /home/meng/ibmcp/collector-cpp/  ← CLion SFTP 上传的源码      │    │
│  │  ├─ collector-cpp/build/             ← CMake 构建目录            │    │
│  │  ├─ venv/                            ← Python 3.11（.so 的 ABI 宿主）│
│  │  │   └─ site-packages/industrial_collector/_core.so  ← 编译产出   │    │
│  │  ├─ gdbserver :1234                  ← CLion 远程调试（唯一调试服务）│
│  │  └─ venv/bin/uvicorn (native)         ← native 联调实例 :8000        │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │ 部署运行层（交付后，systemd + nginx）                            │    │
│  │  ├─ ibmcp-api.service → venv/bin/uvicorn app.main:app :8000      │    │
│  │  ├─ Nginx :80 → 反代 FastAPI + 托管 /home/meng/ibmcp/frontend-dist│    │
│  │  └─ systemd 开机自启                                             │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │ 现场设备 / PLC（Modbus/OPC UA/MQTT/Socket）                      │    │
│  │ 虚拟机通过桥接网络直接访问现场设备 IP                            │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 职责边界（按开发 / 部署分两段）

**开发期**：

| 侧                   | 做什么                                                                                                                                                     | 不做什么                                                    |
|----------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------|
| **Windows 11**       | 写 Python 源码 + **用本地 Python 3.11 跑 FastAPI（stub）** + 写 C++ 源码（CLion 本地索引） + 写前端 + 本地 Vite dev + git + **直连欧拉 PG/Redis 调试**       | 不编 `_core.so`、不装 gcc/cmake、不装 PG/Redis、不跑 native  |
| **openEuler Docker** | 跑 PostgreSQL 15 + Redis 7 + Node-RED（`docker compose up -d`），对 Windows 暴露 5432/6379/1880                                                            | 不跑 FastAPI 开发进程                                       |
| **openEuler 裸机**   | **编译 `_core.so`**（`bash build.sh`）+ 提供 `.so` 的 ABI 宿主 venv + `native` 模式联调 + gdbserver 附着 + 现场设备协议联调                               | 不承载日常 Python 开发（那在 Windows）                      |

**部署期**：

| 侧                   | 做什么                                                                                                              |
|----------------------|---------------------------------------------------------------------------------------------------------------------|
| **openEuler 裸机**   | **承载全部生产负载**：venv + `_core.so` + uvicorn（systemd）+ Nginx + frontend-dist                                  |
| **openEuler Docker** | PG15 + Redis 7 + Node-RED                                                                                           |
| **Windows 11**       | 只通过浏览器访问 `http://192.168.80.134`，不参与运行                                                                |

### 1.3 为什么 Python 开发放 Windows、C++ 编译放欧拉？

这不是偏好，是被三重约束逼出来的最优解：

```
【为什么 Python 能放 Windows】
    backend/ 是纯 Python，无平台绑定
        └─ 唯一平台相关的是 industrial_collector._core（C++ 扩展）
           └─ 由 COLLECTOR_BACKEND 开关隔离：
              stub   → app/core/collector/stub.py（纯 Python，复刻 CollectorApi）
              native → app/core/collector/ffi.py → import industrial_collector._core
        └─ 结论：stub 模式下 Windows 可跑 100% 业务逻辑，不需要欧拉的 Python

【为什么 C++ 必须放欧拉】——三条约束缺一不可
    ① ABI    .so 与 import 它的 Python 必须同版本同 ABI
             → 用 Windows 的 3.11 编译？.so 是 ELF 格式，Windows 加载不了 PE/ELF 不通
    ② glibc  .so 链接欧拉的 glibc + libstdc++ + ELF 动态链接器
             → Windows 无 Linux 动态链接器，即使交叉编译出来也跑不起来
    ③ 驱动   libmodbus.so / libopen62541.so / libmosquitto.so 是 Linux 库
             → Windows 上没有 dnf、没有 .so、没有 pkg-config 描述文件，CMake 直接找不到

【为什么 PG/Redis/Node-RED 放欧拉 Docker】
    现场设备在欧拉所在网段；数据要落在生产同款环境；Windows 不装数据库避免污染本地
        └─ Windows 的 Python 直连 192.168.80.134:5432/6379 即可，配置只改 .env 的三个 host 值
```

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
| SSH  | 安装时开启                        | CLion Remote Host（C++ 编译/调试）+ 数据服务直连                         |

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
# 写入 Windows SSH config（CLion Remote Host / scp 自动识别）
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

> **用户说明**：开发期 SSH（CLion Remote Host、文件上传、数据服务排查）统一用用户 `meng`。但
> `deployments/openeuler/bootstrap.sh` 需要 `root` 权限执行（`sudo bash bootstrap.sh`），因为它要装系统包、装 Docker、配
> Nginx systemd。首次 bootstrap 用 `sudo`。

---

## 三、欧拉服务安装（一键脚本，混合模式）

### 3.1 使用项目自带的 bootstrap.sh

本项目在 `deployments/openeuler/bootstrap.sh` 中提供了一键环境初始化脚本，采用**混合模式**：PG/Redis/Node-RED 走 Docker
Compose，FastAPI/C++/Nginx 裸机安装。

```bash
# —— 在欧拉虚拟机上执行（需要 root）——
# 先把项目同步一次到欧拉（首次，见 §5B.3 用 CLion/PyCharm Deployment 或手动 scp）
# 假设项目已在 /home/meng/ibmcp

sudo bash /home/meng/ibmcp/deployments/openeuler/bootstrap.sh
```

脚本会自动完成以下 7 步：

| 步骤 | 内容                                                                                                                                                                                               |
|------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1/7  | 关闭 SELinux + 防火墙                                                                                                                                                                              |
| 2/7  | dnf 安装裸机工具链：**gcc/g++/cmake/make**（C++ 构建）+ **nginx**（前端静态 + 反代）+ git/wget/curl 等运维工具；可选装协议驱动头文件 `libmodbus`/`open62541`/`mosquitto` 的 `-devel`（缺失时 CMake 自动跳过对应驱动）。**不装** `python3`/`python3-devel`/`postgresql-devel`（Python 用系统自带，PG 走 Docker） |
| 3/7  | 安装 Docker + docker compose 插件，启动 docker 守护进程，当前用户加入 docker 组                                                                                                                    |
| 4/7  | `docker compose up -d` 启动 PG15 + Redis 7 + Node-RED 三个容器（用 `deployments/openeuler/docker-compose.yml`），等待健康检查通过                                                                  |
| 5/7  | 部署 Nginx 配置（`packaging/nginx/ibmcp.conf` → `/etc/nginx/conf.d/ibmcp.conf`），创建前端静态目录 `/home/meng/ibmcp/frontend-dist`，启动 Nginx + 开机自启                                               |
| 6/7  | 校验系统自带 `python3.11` 可用（**不装 uv、不单独装 Python**）；venv 与依赖由后续 `build.sh` 用系统 python 创建，`_core.so` 编译所需 `Python.h` 由 build.sh 按需补装 `python3.11-devel`             |
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
cd /home/meng/ibmcp
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

## 四、欧拉侧：编译 C++ 扩展 + venv

> **本章定位**：欧拉侧的 venv **不是** PyCharm 的远程解释器，而是 **`_core.so` 的 ABI 宿主 + 生产运行时**。
> 日常 Python 开发在 Windows 用本地解释器（见 §五-A）。只有两种情况需要在欧拉跑 Python：
> ① 冒烟测试 `_core.so`（§4.3）；② `native` 模式联调（§7.2）。

### 4.1 使用项目自带的 build.sh

`deployments/openeuler/build.sh` 负责创建 venv + 编译 C++ 扩展 + 安装 backend：

```bash
# —— 在欧拉虚拟机上执行 ——
cd /home/meng/ibmcp
bash deployments/openeuler/build.sh
```

脚本内容（精简）：

```bash
# 创建 venv
python3.11 -m venv venv
source venv/bin/activate

# 安装 C++ 编译依赖（幂等）
dnf install -y cmake gcc-c++ python3.11-devel || true

# 安装 pybind11（必须在 venv 内，供 CMake find_package 查找）
pip install pybind11

# CMake 编译 _core.so 并安装到 python/industrial_collector/
cmake -S collector-cpp -B collector-cpp/build \
    -DCMAKE_BUILD_TYPE=Release \
    -Dpybind11_DIR="$(python -c 'import pybind11; print(pybind11.get_cmake_dir())')" \
    -DPython_EXECUTABLE="$VIRTUAL_ENV/bin/python"
cmake --build collector-cpp/build -j"$(nproc)"
cmake --install collector-cpp/build --prefix python

# 安装包（含 _core.so，由 pyproject.toml 的 package-data 打包）
pip install .
```

> **为什么不再用 `pip install ./python ./backend`**：`python/` 与 `backend/` 都没有自己的
> `pyproject.toml`/`setup.py`，且项目的构建后端是纯 `setuptools.build_meta`，不集成 CMake。
> 因此该命令只可能失败或只装纯 Python 包、产不出 `_core.so`。统一由 `build.sh` 的 CMake 步骤负责编译。

### 4.2 C++ 编译链说明

| 组件         | 版本要求             | 说明                                                                 |
|--------------|----------------------|----------------------------------------------------------------------|
| gcc-c++      | openEuler 24.03 自带 | 支持 C++17                                                           |
| cmake        | ≥ 3.20               | `CMakeLists.txt` 要求 `cmake_minimum_required(VERSION 3.20)`         |
| **C++ 标准** | **C++17**            | `collector-cpp/CMakeLists.txt` 第 5 行：`set(CMAKE_CXX_STANDARD 17)` |
| pybind11     | venv 内 `pip install pybind11` | `find_package(pybind11 CONFIG REQUIRED)`（**不依赖系统 `pybind11-devel`**，用欧拉 venv 里的版本） |
| libmodbus    | libmodbus-devel      | `pkg_check_modules(MODBUS QUIET libmodbus)`                          |
| mosquitto    | mosquitto-devel      | `pkg_check_modules(MOSQUITTO QUIET libmosquitto)`                    |
| open62541    | open62541-devel      | `pkg_check_modules(OPEN62541 QUIET open62541)`（需 1.4+）            |

> **C++ 标准是 C++17，不是 C++14**。`CMakeLists.txt` 明确设置 `CMAKE_CXX_STANDARD 17` + `CMAKE_CXX_STANDARD_REQUIRED ON`。

### 4.3 冒烟测试

编译完成后，验证 `_core.so` 扩展能加载：

```bash
cd /home/meng/ibmcp
source venv/bin/activate

# 验证 C++ 扩展直接加载（health 挂在 Collector 单例上，模块级没有 health()）
python -c "import industrial_collector._core as c; print('✅ _core loaded:', c.Collector.instance().health())"

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

| 取值     | 含义                                                   | 使用场景                                     |
|----------|--------------------------------------------------------|----------------------------------------------|
| `stub`   | `StubCollector`（纯 Python，无 `_core.so`）            | **Windows 本地开发**、CI、无硬件联调、单元测试 |
| `native` | `NativeCollector`（加载 `industrial_collector._core`） | **欧拉**：`native` 联调 + 生产运行            |

> **这是本方案的核心开关**。Windows 上 `COLLECTOR_BACKEND` 必须为 `stub`——因为 Windows 无法 import Linux 的 `.so`。
> 一旦误设为 `native`，会立刻抛出 `RuntimeError: industrial_collector._core 未安装`（见 `ffi.py` 的 `_ensure_loaded`）。
> 欧拉侧才用 `native`。

> **不是 `ffi`**。`backend/app/core/collector/ffi.py` 的 `get_collector(backend)` 函数接受 `"stub"` 或 `"native"`，其他值抛
> `ValueError`。

---

## 五、Windows 开发配置

本章分两部分：**§5-A 是 Python 主体（本地解释器）**，**§5-B 是 C++ 侧（CLion Remote Host）**。

### 5-A. PyCharm：本地 Python 解释器 + 前端 + 数据库

**前置条件**：Windows 安装 **uv**（`irm https://astral.sh/uv/install.ps1 | iex`，或直接跑 §5A.1 的一键脚本，会自动装 uv）和 PyCharm Professional 2026.3（DB 面板为 Pro 功能；纯 Python 开发用 Community 也可以）。**不需要手工装 Python**——解释器由 uv 按 `.python-version` 托管。

#### 5A.1 创建 Windows 本地 Python 环境（uv 管理，推荐一键脚本）

> 项目根 `.python-version` 锁定 **3.11.18**（与 `backend/.python-version` 一致）；`uv sync` 在项目根运行，会自动识别它。**无需手工 `py -m venv` / `pip install`**。

```powershell
cd E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP

# —— 方式一（推荐）：一键脚本，自动检测/装 uv + 装 3.11.18 + uv sync ——
powershell -ExecutionPolicy Bypass -File deployments\dev-windows\bootstrap.ps1

# —— 方式二：手工（等价流程）——
# 1) 确认 uv 与锁定的解释器
uv --version
uv python list                       # 应能看到 3.11.18

# 2) 一键同步：按 uv.lock 在项目根创建/更新 .venv，安装全部依赖（含 dev 组）
uv sync

# 3) 验证
uv run python --version              # 应为 Python 3.11.18
uv run python -c "import fastapi, uvicorn, asyncpg, pydantic_settings; print('deps OK')"
```

> **uv 做了什么**：读项目根 `pyproject.toml` + `.python-version`，自动下载并锁定 **Python 3.11.18**，在项目根创建 `.venv`，按 `uv.lock` 精确复现依赖。日常改完 `pyproject.toml` 后跑 `uv sync` 即可；新装依赖用 `uv add <pkg>`。

> **为什么不装 `industrial_collector` 的 `_core.so`**：Windows 上 `uv sync` 只提供纯 Python 包与接口，不含 `_core.so`；
> `_core.so` 由欧拉编译（§4.1），Windows 永远拿不到也不需要。`COLLECTOR_BACKEND=stub` 时不 import 它。

#### 5A.2 配置 PyCharm 解释器（本地，非 SSH）

```
Ctrl+Alt+S → Python | Interpreter → Add Interpreter → Add Local Interpreter

步骤 1：选 "Virtualenv Environment" → "Existing"
步骤 2：Interpreter 填：E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\.venv\Scripts\python.exe
步骤 3：OK
```

> **注意**：这里**不再选 "On SSH"**。开发期 Python 全部在 Windows 跑，PyCharm 用本地解释器建索引、跑调试、开终端。

#### 5A.3 Windows 本地 .env 配置（连欧拉的数据服务）

```powershell
cd E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\backend
Copy-Item .env.example .env
```

编辑 `backend/.env`，**只需改三处 host + 确认 stub**：

```ini
# 关键：Windows 本地 → 欧拉 Docker
PG_HOST=192.168.80.134
PG_PORT=5432
REDIS_URL=redis://192.168.80.134:6379/0

# 关键：Windows 上必须是 stub（无 _core.so）
COLLECTOR_BACKEND=stub

# 其他保持默认
APP_ENV=development
APP_HOST=127.0.0.1
APP_PORT=8000
```

> **为什么改 host**：Windows 上 `127.0.0.1:5432` 指的是 Windows 自己，没有 PG。必须指向欧拉
> `192.168.80.134`（`docker-compose.yml` 已把容器端口映射到欧拉宿主所有网卡）。
>
> **部署时改回来**：欧拉侧 `backend/.env` 或 systemd `Environment=` 用 `PG_HOST=127.0.0.1`。

#### 5A.4 一键启动本地开发（不依赖 PyCharm）

```powershell
# 方式一：PyCharm Run Config（推荐日常用，见 §7.1.1）
# 方式二：命令行（uv 会自动用项目根 .venv，无需手动激活）
cd E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\backend
uv run uvicorn app.main:app --reload --host 127.0.0.1 --port 8000
# 传统激活方式也可用：..\.venv\Scripts\Activate.ps1 ; uvicorn app.main:app --reload

# 浏览器
# http://127.0.0.1:8000/api/v1/docs
```

> **必须 `cd backend` 再启动**：`config.py` 用 `env_file=".env"` 相对路径读配置，不在 `backend/` 下启动就读不到 `.env`。

#### 5A.5 PyCharm 内置数据库工具（连欧拉 PG/Redis）

PyCharm 右侧边栏 Database → + → Data Source：

| 服务                     | Host             | Port   | DB      | User    | Password | 备注                        |
|--------------------------|------------------|--------|---------|---------|----------|-----------------------------|
| PostgreSQL               | `192.168.80.134` | `5432` | `ibmcp` | `ibmcp` | `ibmcp`  | 欧拉 Docker 容器，直连      |
| Redis（装 Redis 插件后） | `192.168.80.134` | `6379` | —       | —       | —        | 欧拉 Docker 容器，直连      |

> **直连即可，无需 SSH 隧道**：欧拉在桥接网段，Windows 能直接访问 5432/6379。若要更安全可用 SSH 隧道（§6）。

### 5-B. CLion：C++ 源码在 Windows，编译/调试在欧拉

**前置条件**：Windows 安装 CLion 2026.3（Remote Host 是 Ultimate 版特性）

#### 5B.1 配置 Remote Host Toolchain（编译在欧拉执行）

```
Ctrl+Alt+S → Build, Execution, Deployment | Toolchains → + → Remote Host
        Credentials → 选已配好的 SSH 配置 "euler-ibmcp"
        CMake executable: /usr/bin/cmake
        C++ Compiler:     /usr/bin/g++
        Debugger:         /usr/bin/gdb
→ 点 "Test Connection" → CLion 自动在欧拉上装 C++ Remote Debugger
```

> **这一步是本方案的关键**：CLion 用 Remote Host Toolchain，意味着 **CMake 配置、编译、链接全在欧拉执行**，
> Windows 只做编辑与索引。产物 `_core.so` 天然是 Linux ELF，与欧拉 venv 的 ABI 一致。

#### 5B.2 配置 Deployment + CMake Profile（只上传 collector-cpp）

```
Ctrl+Alt+S → Build, Execution, Deployment | Deployment
  （或菜单：Tools | Deployment | Configuration）

步骤 1：点左上角 + → 选 SFTP → 弹出 Create New Server → 输入名称 "euler-ibmcp" → OK

步骤 2：Connection 标签页：
    SSH configuration: 点 ... 浏览 → 选已配好的 SSH 配置
                       （或新建：Host 192.168.80.134 / Port 22 / User meng / Key pair）
    Root path: /home/meng/ibmcp
        （可点 Autodetect 自动探测，或手动填 + 点 Browse 确认）
    Web server URL: （留空，本项目不需要）

步骤 3：Mappings 标签页：
    Local path:        E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP
    Deployment path:   /          （相对于 Root path，即 /home/meng/ibmcp）
    Web path:          /          （留空即可）

步骤 4：选中 euler-ibmcp → 点工具栏 "Use as Default" 按钮
        （设为默认服务器，自动上传只会传到默认服务器）

步骤 5：Options（Ctrl+Alt+S → Build, Execution, Deployment | Deployment | Options
        或菜单 Tools | Deployment | Options）：
    Upload changed files automatically to the default server: Always
    Delete remote files when local are deleted: ✅（保持镜像一致）
    Skip external changes: ✅（排除 VCS/脚本产生的外部变更，避免误传）
```

#### 5B.3 首次上传 C++ 源码（只需同步 collector-cpp）

配好 Deployment 后做一次**首次上传**，把 `collector-cpp/`（以及欧拉编译需要的 `python/`、`pyproject.toml`）推到欧拉。
之后每次保存 C++ 文件即自动 upload。

> **为什么不用同步 `backend/`**：Python 代码在 Windows 本地跑（§5A），开发期不需要出现在欧拉。只有**部署时**才需要把
> `backend/` 放到欧拉（走 `git` 或一次性全量上传，见 §13 部署章节）。

**操作步骤（CLion / PyCharm 2026.3 界面）**：

```
步骤 1：打开 Project 工具窗口（Alt+1）
        → 选中 collector-cpp 目录（或项目根节点）

步骤 2：右键 → Deployment | Upload to
        → 选 "euler-ibmcp"

        ※ 若已设为默认服务器，菜单直接显示 "Upload to euler-ibmcp"

步骤 3：底部 "File Transfer" 窗口实时显示传输列表，全量上传约 10~30 秒
步骤 4：完成后看统计 "N files uploaded (M successes, K failures)"
        → 失败项右键 → Retry Upload
```

**验证文件已到达欧拉侧**：

```bash
ssh meng@192.168.80.134

# C++ 编译必需的文件
ls /home/meng/ibmcp/collector-cpp/CMakeLists.txt
ls /home/meng/ibmcp/collector-cpp/src/
ls /home/meng/ibmcp/pyproject.toml

# 确认排除项没有传上去
ls /home/meng/ibmcp/.venv/                    # 应不存在（Windows venv 不该传）
ls /home/meng/ibmcp/frontend/                 # 应不存在（前端源码不上传，只传 dist 产物到 frontend-dist）
ls /home/meng/ibmcp/backend/app/__pycache__/  # 应不存在
ls /home/meng/ibmcp/collector-cpp/build/      # 应不存在（欧拉侧自己生成）
```

**确认自动同步已生效**：

```
步骤 1：CLion 打开 collector-cpp/src/ 下任一 .cpp
步骤 2：加一行注释 → Ctrl+S 保存
步骤 3：File Transfer 窗口应出现新传输记录
步骤 4：欧拉侧验证
        grep "sync test" /home/meng/ibmcp/collector-cpp/src/*.cpp
步骤 5：删掉测试注释 → Ctrl+S → 确认再次自动上传
```

> **常见问题**：
> - **右键菜单没有 "Deployment" 项**：确认 PyCharm/CLion Professional 版（Community 版无 Deployment 功能）；确认 FTP/SFTP/WebDAV Connectivity 插件已启用（`Ctrl+Alt+S → Plugins → Installed → 搜索 SFTP`）；确认已配置至少一个
>   SFTP 连接（§5B.2）
> - **上传失败 "Permission denied"**：欧拉侧 `/home/meng/ibmcp` 目录 owner 不是 `meng`，执行
>   `sudo chown -R meng:meng /home/meng/ibmcp`
> - **上传失败 "Could not connect"**：检查 SSH 免密是否配好（09 号 §2.4）；`ssh meng@192.168.80.134` 能否手动连通
> - **保存后没有自动上传**：`Tools | Deployment | Options` → "Upload changed files automatically to the default server" 确认设为
>   "Always"；确认 euler-ibmcp 已设为默认服务器
> - **传了不该传的文件**：见下方排除项，配好排除后重新上传一次

**排除项**（必须有，否则 Windows venv/缓存会被传到欧拉）：

```
Ctrl+Alt+S → Build, Execution, Deployment | Deployment | Options → Excluded Paths → +
    E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\.venv
    E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\venv
    E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\frontend
    E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\collector-cpp\build
```

> - `.venv/`、`venv/`（Windows 的 venv 绝不传到欧拉）
> - `frontend/` 整个目录（开发期不上传；部署期用 scp 把 `frontend\dist\*` 传到 `frontend-dist`）
> - `__pycache__/`、`.ruff_cache/`
> - `ibmcp.egg-info/`
> - `collector-cpp/build/`（C++ 编译临时，欧拉侧自己生成）
> - `.git/`（git 操作只在 Windows 做）

#### 5B.4 欧拉终端环境（仅 C++ 编译/verification 用）

在欧拉侧，`dev-env.sh` 提供 venv 激活 + 别名 + 提示符标记，主要用于**编译 `_core.so`、冒烟测试、`native` 联调**：

```bash
# 在欧拉终端（ssh meng@192.168.80.134）
source /home/meng/ibmcp/deployments/openeuler/dev-env.sh
```

| 别名 | 等价命令 | 用途 |
|------|----------|------|
| `ibmcp-build` | `bash deployments/openeuler/build.sh` | **编译 `_core.so` + 安装包**（最主要用途） |
| `ibmcp-api-native` | `COLLECTOR_BACKEND=native uvicorn ...` | **`native` 联调**（验证 C++ 扩展） |
| `ibmcp-up` / `ibmcp-down` / `ibmcp-logs` | `docker compose -f deployments/openeuler/docker-compose.yml ...` | PG/Redis/Node-RED |
| `ibmcp-api` | `uvicorn app.main:app --reload` | 欧拉侧启动（**仅部署后或排查环境用**） |
| `ibmcp-test` / `ibmcp-migrate` | `pytest` / `alembic upgrade head` | 部署期用 |

**让它每次开终端都自动生效**（只需一次）：

```bash
echo 'source /home/meng/ibmcp/deployments/openeuler/dev-env.sh' >> ~/.bashrc
```

> **必须用 `source`（或 `.`）执行**。直接 `bash dev-env.sh` 只影响子 shell，脚本一退出激活状态就消失。
>
> **脚本必须对非交互会话完全静默**（已内置于 `dev-env.sh`：非 `-i` 且 stdout 非 tty 时只加载不打印）。原因：一旦
> `~/.bashrc` 里的 hook 往 stdout 打任何字，`scp`/`sftp` 协议握手会被污染，报
> `Received message too long` + `Ensure the remote shell produces no output for non-interactive sessions`，
> 导致 **CLion/PyCharm Deployment 上传 / Auto-upload 全部失败**。改动脚本后务必自检（必须输出 `0`）：
>
> ```bash
> bash -lc 'true' | wc -c
> ```
>
> 需临时禁用 hook 时（先备份）：
>
> ```bash
> cp ~/.bashrc ~/.bashrc.bak
> grep -vx 'source /home/meng/ibmcp/deployments/openeuler/dev-env.sh' ~/.bashrc > ~/.bashrc.new && mv ~/.bashrc.new ~/.bashrc
> ```
>
> **不要用 PowerShell 管道把脚本内容喂给远端 `cat`**（`Get-Content -Raw x.sh | ssh ... "cat > x.sh"`）：PowerShell 会按
> 本地代码页转码，中文注释变 `?`、换行丢失，远端脚本直接语法错误。传文件一律用 `scp`。

### 5C. 目录映射与双环境纪律

```
Windows 本地路径（源头 + Python 运行端）        ←→  欧拉路径（C++ 编译端 + 部署运行端）
E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\
  ├─ backend\app\        ← 本地 Python 跑（stub）       ←→  /home/meng/ibmcp/backend/app/     ← 仅部署时需要
  ├─ frontend\           ← 本地 Vite dev                ←→  /home/meng/ibmcp/frontend/        ← 仅部署时 build
  ├─ collector-cpp\      ← CLion 编辑                   ←→  /home/meng/ibmcp/collector-cpp/   ← 🔁 自动同步
  ├─ python\             ← 源码编辑                     ←→  /home/meng/ibmcp/python/         ← 🔁 自动同步
  ├─ deployments\        ← 脚本编辑                     ←→  /home/meng/ibmcp/deployments/    ← 🔁 自动同步
  ├─ .venv\              ← Windows 本地 venv（排除）    ←→ （不同步）
  └─ pyproject.toml      ← 源码编辑                     ←→  /home/meng/ibmcp/pyproject.toml  ← 🔁 自动同步
```

| #  | 纪律                                                            | 理由                                                 |
|----|-----------------------------------------------------------------|------------------------------------------------------|
| D1 | **代码只在 Windows 编辑**（PyCharm/CLion）                      | Windows 是唯一源头                                   |
| D2 | **C++ 源码保存即自动 upload**（CLion SFTP）                    | 欧拉编译需要最新 C++ 源码                            |
| D3 | **Python 代码开发期不需要上传**                                 | Python 在 Windows 本地跑（stub），上传无意义         |
| D4 | **不在欧拉侧手动改代码**                                        | 会被下次同步覆盖或部署时被覆盖                       |
| D5 | **git 只在 Windows 执行**                                       | 欧拉部署用 `git pull` 或一次性上传                   |
| D6 | `.venv/`、`venv/`、`node_modules/`、`build/` **绝不同步**       | 机器绑定的二进制和路径                               |
| D7 | CLion Deployment 设为 **Never download from server**            | 防止欧拉侧 CMakeCache 反向下拉污染 Windows 索引      |
| D8 | **两侧 `COLLECTOR_BACKEND` 不同**：Windows=`stub`、欧拉=`native` | Windows 无 Linux `.so`；欧拉有                     |

---

## 六、SSH 隧道与端口映射

### 6.1 端口总览

| 端口 | 服务                  | 跑在哪                        | 谁在用                                          |
|------|-----------------------|-------------------------------|--------------------------------------------------|
| 22   | SSH 本身              | 欧拉宿主                      | CLion Remote Host / 文件上传 / 手工运维          |
| 5432 | PostgreSQL 15         | **欧拉 Docker** `ibmcp-postgres` | **Windows 本地 Python** / PyCharm DB / DBeaver  |
| 6379 | Redis 7               | **欧拉 Docker** `ibmcp-redis`    | **Windows 本地 Python** / Redis Insight         |
| 1880 | Node-RED              | **欧拉 Docker** `ibmcp-nodered`  | 浏览器                                          |
| 8000 | FastAPI（HTTP + WS）  | **Windows 本地**（开发期）<br>**欧拉**（部署/联调） | Chrome / Vite proxy / Nginx 反代 |
| 80   | Nginx 反代 + 前端静态 | **欧拉裸机**（部署期）        | Chrome（生产）                                  |
| 5678 | ~~debugpy~~（**已废弃**） | —                         | Python 调试改在 Windows 本地，不再需要                          |
| 1234 | gdbserver             | **欧拉**                      | CLion Remote Debugger                           |
| 5173 | 前端 Vite dev server  | **Windows 本地**              | Chrome                                          |

**核心**：开发期 Python 在 Windows（8000/5173 在本地），C++ 编译 + 数据服务在欧拉（1234/5432/6379/1880 在欧拉），
Windows 直连欧拉端口即可，**不需要 SSH 隧道**。

### 6.2 隧道（可选，仅用于安全加固）

> 桥接网络下 Windows 已能直连 `192.168.80.134:5432/6379/1880`，隧道**不是必需**。仅在以下场景需要：① 不想在欧拉防火墙上
> 开放端口；② 出差/远程接入；③ 想用 `127.0.0.1` 统一口径。

```
# $env:USERPROFILE\.ssh\config
Host euler-ibmcp
    HostName 192.168.80.134
    User meng
    Port 22
    IdentityFile ~/.ssh/ibmcp_euler
    ServerAliveInterval 30

    # 仅在需要 "127.0.0.1 口径" 时启用；日常直连即可不开
    LocalForward 5432 localhost:5432    # PG       (欧拉 Docker)
    LocalForward 6379 localhost:6379    # Redis    (欧拉 Docker)
    LocalForward 1880 localhost:1880    # Node-RED (欧拉 Docker)
    LocalForward 1234 localhost:1234    # gdbserver(欧拉)
```

#### 方式 B：不用隧道，直接连宿主机 IP

Windows 本地 `.env` 里写 `PG_HOST=192.168.80.134`（§5A.3），彻底绕开隧道的额外维护成本。**这是本方案的默认做法**。

#### 方式 C：`ssh -L` 临时开

```powershell
ssh -L 5432:localhost:5432 -L 6379:localhost:6379 meng@192.168.80.134
```

### 6.3 连通性验证

```powershell
# 直连验证（开发期默认方式：Windows → 欧拉）
foreach ($p in 5432, 6379, 1880, 1234) {
    $r = Test-NetConnection -ComputerName 192.168.80.134 -Port $p -WarningAction SilentlyContinue
    Write-Host "192.168.80.134:${p} -> $($r.TcpTestSucceeded)"
}

# 若走了隧道，再验 127.0.0.1
foreach ($p in 5432, 6379) {
    $r = Test-NetConnection -ComputerName 127.0.0.1 -Port $p -WarningAction SilentlyContinue
    Write-Host "127.0.0.1:${p} -> $($r.TcpTestSucceeded)"
}

# 浏览器
# http://127.0.0.1:8000/api/v1/docs      → Windows 本地 FastAPI（开发期）
# http://192.168.80.134:8000/api/v1/docs → 欧拉 FastAPI（部署/native 联调）
# http://192.168.80.134:1880             → Node-RED（欧拉 Docker）
```

> **8000 端口注意**：开发期 FastAPI 跑在 Windows，`127.0.0.1:8000` 是本地；`192.168.80.134:8000` 只在欧拉侧起了服务时才
> 有响应（部署后或 `native` 联调时）。

---

## 七、调试链路详解

### 7.1 Python 代码：PyCharm **本地**调试

> **新架构关键变化**：Python 调试**不再走远程**。断点、变量、单步全部在 Windows 本地进程上，比 SSH 远程调试快得多、也更
> 稳定（无网络抖动导致的断点失效）。
>
> ⚠️ **注意区分**：这里的 `debugpy` 是 **PyCharm 本地调试器的内部实现机制**（IDE 与同机 python.exe 之间的本地 socket），
> **不是**需要单独启动的 `python -m debugpy --listen :5678` 远程调试服务。**欧拉侧不再监听 5678 端口**。

#### 7.1.1 创建 Run/Debug Configuration（**本地 Interpreter**）

```
Run → Edit Configurations → + → Python

# Profile A（日常默认）：stub 模式，连欧拉 Docker 的 PG/Redis
Name: FastAPI (local, stub)
Module name: uvicorn
Parameters: app.main:app --host 127.0.0.1 --port 8000 --reload
Working directory: E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\backend
Python interpreter: Python 3.11 (02-IBMCP)  ← 本地 .venv（§5A.2）
Environment variables（或在 backend\.env 里配，二选一）：
  COLLECTOR_BACKEND=stub
  PG_HOST=192.168.80.134
  PG_PORT=5432
  PG_USER=ibmcp
  PG_PASSWORD=ibmcp
  PG_DATABASE=ibmcp
  REDIS_URL=redis://192.168.80.134:6379/0
Path mappings: 无（不需要，本地 to 本地）
```

C++ 相关的调试走 **§7.2 CLion**，与这个 Run Config 互不干扰。

#### 7.1.2 启动调试

```
PyCharm → Debug（虫子图标）
  ├─ Windows 本地启动 python.exe -m uvicorn ...（端口 8000）
  ├─ debugpy 通过本地 socket 连 IDE
  ├─ 断点/变量/单步 全在本地，零网络延迟
  └─ 业务代码调 C++ 时（如果装了 .so）会直接抛 RuntimeError
     → 所以本地一律 stub
```

> **本地调试连的是欧拉的 DB**：`PG_HOST=192.168.80.134` → 走 Windows 网卡 → 欧拉 `docker-proxy` → 容器内 PG。
> 数据是**真实的、唯一的**（不存在"本地库和欧拉库不一致"问题，因为只有一个库）。

> **关键对齐点**：
> - **启动命令是 `app.main:app`**（不是 `app.main:create_app --factory`）。`backend/app/main.py` 同时定义了 `create_app()`
>   函数和模块级 `app = create_app()`，systemd service 和 Dockerfile 都用 `app.main:app`。
> - **`COLLECTOR_BACKEND` 取值是 `native` / `stub`**（不是 `ffi`）。见 §4.4。
> - **PG 连接用 `PG_HOST` 等独立变量**，不是 `DATABASE_URL`。`config.py` 的 `pg_dsn` 属性会拼成
>   `postgresql://ibmcp:ibmcp@192.168.80.134:5432/ibmcp`。
> - **Working directory 是 `backend` 目录**（Windows 本地），因为 `config.py` 用相对路径 `env_file=".env"`。

> **`COLLECTOR_BACKEND` 的选择逻辑与 stub 实现细节**（stub 如何复刻 CollectorApi 全部方法、`ffi.py` 的 `get_collector()`
> 如何分发、stub.py 与 protocol.py 的 Protocol
> 统一接口）全部以 [05-绑定契约 stub/native 章节](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md)
> 为权威，本文档只保留开发环境的 PyCharm Run/Debug 配置步骤。

### 7.2 C++ 扩展：CLion gdbserver 远程附着

#### 7.2.1 准备：放开 ptrace

```bash
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
echo "kernel.yama.ptrace_scope = 0" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

#### 7.2.2 附着步骤（CLion Remote Debug）

C++ 扩展的宿主进程是**欧拉侧的 Python**，所以调试对象也在欧拉：

```bash
# 欧拉侧 Terminal（ssh meng@192.168.80.134）
cd /home/meng/ibmcp && source venv/bin/activate
cd backend

# 用 native 模式起 FastAPI（加载 _core.so）
COLLECTOR_BACKEND=native uvicorn app.main:app --host 0.0.0.0 --port 8000 &
PID=$!

# gdbserver 附着到这个进程
gdbserver :1234 --attach $PID
```

> **为什么必须 `--host 0.0.0.0`**：`native` 联调时 Windows 上的 Vite / 浏览器可能要直连这个实例。

CLion 侧（**利用 Remote Host Toolchain，无需端口转发**）：

```
Run → Attach to Process → Remote Host → euler-ibmcp
→ 自动列出欧拉上的进程（gdbserver / python）
→ 选 gdbserver :1234（CLion Remote Host 模式下自动隧道，无需手工填 Host:Port）
→ 自动加载 CMake Profile 符号表
→ 在 collector-cpp/src/*.cpp 设断点 → 触发采集 → 断点接住
```

> **若 CLion 未自动探测**：改用 `Run → Attach to Process → 手动输入`，Host 填 `192.168.80.134`，Port 填 `1234`。
> 或者用 `Run → Debug Remote Target`（CLion 2026.3 的 Remote Host 目标调试入口）。

#### 7.2.3 调试符号说明

日常 `bash deployments/openeuler/build.sh` 产出的是 Release 二进制（无 `-g` 符号），CLion gdb 断点会报 "No executable code
found"。需要 C++ 单步时切换 Debug 编译：

```bash
cd /home/meng/ibmcp
source venv/bin/activate

# Debug 编译（带 -g 符号）
PYBIND11_DIR="$(python -c 'import pybind11; print(pybind11.get_cmake_dir())')"
cmake -S collector-cpp -B collector-cpp/build \
    -DCMAKE_BUILD_TYPE=Debug \
    -Dpybind11_DIR="$PYBIND11_DIR" \
    -DPython_EXECUTABLE="$VIRTUAL_ENV/bin/python"
cmake --build collector-cpp/build -j"$(nproc)"
cmake --install collector-cpp/build --prefix python
pip install . --force-reinstall --no-deps
```

### 7.3 C++ 与 Python 跨语言混合断点（欧拉侧）

> **新架构下这条链路只在欧拉做**（Windows 的 stub 没有 C++ 可断）。用于排查 `_core.so` 与 Python 交互的死锁/崩溃。

```
步骤 1：CLion 用 Debug-Remote CMake Profile 编译（带 -g）→ 自动上传到 /home/meng/ibmcp
步骤 2：欧拉终端用 dev-env.sh 起 native 实例并挂 gdbserver（§7.2.2）
步骤 3：CLion Run → Attach to Process → Remote Host → euler-ibmcp → gdbserver :1234
        → 在 collector-cpp/src/ 设 C++ 断点
步骤 4：用另一条 SSH 终端打请求（curl 或 Python 脚本）触发采集
        → C++ 断点接住 → 单步 → 继续执行回 Python
步骤 5：调试完毕 → 先停 gdbserver（Ctrl+C），再 kill uvicorn
```

> **为什么不用 PyCharm 同时断 PY 侧**：Python 的调试面在 Windows（§7.1），欧拉侧进程是"被调试宿主"而非开发入口。
> 需要跨语言联调时，在 `_core` 的入口处插桩打日志（`std::cerr` 或 `py::print`），比双 IDE 同时断点更省事。
>
> **GIL 与死锁规则**（什么时候必须 `py::gil_scoped_release`、`stop()` 为什么会死锁、DataStore 为什么不能包
> GIL）全部在 [05-绑定契约 GIL 规则表](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md)
> 中有明确定义，"请求卡死不动" 是 `stop()` 死锁的典型表现。

---

## 八、前端调试与部署

### 8.1 开发期：Vite 与 FastAPI 同在 Windows 本地

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
  port: 5173,            // ← 不是 3000
  strictPort: true,
  proxy: {
    '/api': {
      target: 'http://127.0.0.1:8000',   // ← 默认：Windows 本地 FastAPI（stub）
      changeOrigin: true,
    },
    '/ws': {
      target: 'ws://127.0.0.1:8000',
      ws: true,
      changeOrigin: true,
    },
  },
}
```

> **两种 proxy target 的口径**（**只能二选一，改完必须重启 Vite**）：

| 场景 | `target` | 说明 |
|------|----------|------|
| **日常开发（stub）** | `http://127.0.0.1:8000` | FastAPI 跑在 Windows 本地，代理不跨网，最快 |
| **`native` 联调** | `http://192.168.80.134:8000` | FastAPI 跑在欧拉（`COLLECTOR_BACKEND=native`） |

> 想不改配置文件就切换，可用环境变量：`VITE_API_TARGET=http://192.168.80.134:8000 npm run dev`

数据流：

```
日常：浏览器 → Vite(Windows:5173) → proxy → 127.0.0.1:8000（Windows FastAPI, stub）→ 欧拉 PG/Redis
联调：浏览器 → Vite(Windows:5173) → proxy → 192.168.80.134:8000（欧拉 FastAPI, native）
部署：浏览器 → 欧拉 Nginx:80 → (静态 frontend-dist) + /api 反代 → 127.0.0.1:8000（欧拉 FastAPI）
```

### 8.2 部署期：前端在 Windows 构建，静态产物上传到欧拉 Nginx（欧拉不装 Node、不执行 `npm run build`）

前端**只在 Windows 侧构建**，欧拉侧不安装 Node.js / npm、也不在欧拉执行 `npm run build`。欧拉只接收 `dist/` 静态产物
（HTML/JS/CSS），放到 Nginx 静态目录（`/home/meng/ibmcp/frontend-dist`，由 `packaging/nginx/ibmcp.conf` 的 `root` 指定）。

**① Windows 侧构建**：

```powershell
# —— 在 Windows PowerShell 执行 ——
cd "E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\frontend"

npm install        # 首次或依赖变更时
npm run build      # 产出 frontend\dist\
```

**② 上传产物到欧拉 Nginx 静态目录**：

```powershell
scp -r -i "$env:USERPROFILE\.ssh\ibmcp_euler" `
    "E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\frontend\dist\*" `
    meng@192.168.80.134:/home/meng/ibmcp/frontend-dist/
```

> 也可以用 Deployment 自动上传，但**映射源要设成 `frontend\dist\` 而不是 `frontend\`**，避免把
> `node_modules/`、`src/` 一起推到欧拉。

**③ 欧拉侧重载 Nginx**：

```bash
# —— 在欧拉虚拟机上执行 ——
chmod -R o+rX /home/meng/ibmcp/frontend-dist

# Nginx 配置已在 bootstrap.sh 中部署到 /etc/nginx/conf.d/ibmcp.conf
# 重载 Nginx（无中断）
sudo nginx -t && sudo nginx -s reload
```

> **为什么欧拉不装 Node.js**：构建只产出一次静态文件，产物与平台无关；`npm install` 会额外占用几百 MB 依赖并常驻
> `node_modules/`，而欧拉磁盘要留给 PG 数据卷、Docker 镜像和 C++ 编译临时文件。

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
    root /home/meng/ibmcp/frontend-dist;     # ← 前端静态文件目录
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

> **前端更新流程**：每次前端代码改完需要部署时，在 **Windows 侧** `npm run build`，再把 `frontend\dist\*` 上传到
> `/home/meng/ibmcp/frontend-dist/`，最后欧拉侧 `nginx -s reload`。开发期用 Vite dev server（§8.1）无需频繁部署。

### 8.3 关于「Vite dev server 跑在欧拉」——不推荐且默认不启用

理论上可以在欧拉 `npm install && npm run dev`，但本方案**不这么做**，原因有三：

1. **欧拉不装 Node.js / npm**（见 §0.1 分工表）。为跑一个 dev server 单独装 Node 会引入几百 MB 依赖和
   `node_modules/`，与「欧拉只承载 C++ 编译 + 数据服务 + Nginx 静态托管」的定位冲突。
2. **HMR 延迟**：Vite 热更新要跨网络往返，改一行等几百毫秒，不如 Windows 本地毫秒级热更。
3. **没必要**：前端 dev server 与后端 FastAPI 之间的通道已经有 `VITE_API_TARGET` 统一控制，
   Windows 跑 Vite + 直连欧拉后端（或本地 stub）即可覆盖全部联调场景。

**唯一需要用到欧拉 Nginx 的场景**是「验证生产形态部署」，那时用的是 **Windows 构建出的 `dist/` 静态产物**
（见 §8.2），而不是 dev server。

---

## 九、开发工作流

### 9.1 每天启动 checklist

```
□ VMware → 启动 openEuler 虚拟机 → 确认 IP 192.168.80.134
□ Docker 容器：ssh meng@192.168.80.134 "docker ps"
    → ibmcp-postgres / ibmcp-redis / ibmcp-nodered 全部 Up
    （没起来就 docker compose -f deployments/openeuler/docker-compose.yml up -d）
□ 裸机 Nginx：仅部署后需要 → systemctl status nginx（开发期可跳过）
□ Windows 本地（uv 创建 .venv）：在项目根执行 `uv run python --version`  → **3.11.18**
□ PyCharm → 打开 02-IBMCP → 右下角解释器显示 "Python 3.11 (02-IBMCP)"（**不是 SSH**）
□ PyCharm → 点 Debug → 控制台出现 "Uvicorn running on http://127.0.0.1:8000"
□ CLion → 打开 collector-cpp → Toolchain 显示 "Remote Host (euler-ibmcp)" 且 CMake 已 reload
□ 连通性：Test-NetConnection 192.168.80.134 -Port 5432 / 6379 / 1880
□ 浏览器：
    http://127.0.0.1:8000/api/v1/docs  → Windows 本地 FastAPI（开发期主入口）
    http://localhost:5173              → Windows 前端 Vite dev
    http://192.168.80.134:1880         → Node-RED（欧拉 Docker）
    http://192.168.80.134              → 前端（仅部署后，Nginx）
□ PyCharm Database → ibmcp@192.168.80.134 → 表结构正常
```

### 9.2 Python 开发循环（全本地，最快）

```
PyCharm（Windows 本地索引 + 本地解释器）
    │ 编辑 backend/app/services/plan_compiler.py（毫秒级索引/补全）
    │ Ctrl+S → uvicorn --reload 自动重启（本地进程，无需上传）
    │ 点 Debug → 断点在 Windows 本地命中，零网络延迟
    │ 接口读写的是欧拉 Docker 里的真实 PG/Redis（COLLECTOR_BACKEND=stub）
    │ 浏览器 http://localhost:5173 看前端实时监控
```

> **开发期不产生任何上传流量**。这是新架构相对旧"SSH 远程解释器"的最大收益：保存 → 重启 → 断点，全在本机内存里完成。

### 9.3 C++ 开发循环（编译在欧拉）

```
CLion Remote Host（Windows 本地索引）
    │ 编辑 collector-cpp/src/scheduler/io_pool.cpp
    │ Ctrl+S → CLion SFTP upload 到 /home/meng/ibmcp/collector-cpp/
    │ 欧拉侧 Terminal: cd /home/meng/ibmcp && bash deployments/openeuler/build.sh（Release）
    │   或改 -DCMAKE_BUILD_TYPE=Debug 重走 CMake（调试，见 §7.2.3）
    │ gdbserver :1234 --attach $(pgrep -f uvicorn)
    │ CLion Attach → 断点 → 单步跟踪
```

### 9.4 联调顺序（**严格按此顺序**）

```
阶段 1：Windows + stub（无硬件，日常最主要）
  └─ 前端页面、配置向导、历史查询、控制下发 UI
     PyCharm Debug 选 "FastAPI (local, stub)"
     验证整条 Python 业务链跑通（DB 是欧拉的真实库）

阶段 2：欧拉 + native + 无设备（验证 C++ 扩展）
  └─ bash deployments/openeuler/build.sh 编译成功、import 正常
     COLLECTOR_BACKEND=native 起服务 → NativeCollector.init() / list_protocols() / health()
     空 plan 下发 + apply_plan 返回 0
     （Windows 侧的 Vite/HTTP Client 可直连 192.168.80.134:8000 做接口联调）

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
cd /home/meng/ibmcp && source venv/bin/activate

bash deployments/openeuler/build.sh                                     # Release，日常（含 venv + CMake + 安装）
rm -rf collector-cpp/build && bash deployments/openeuler/build.sh       # 完整清理后重编

python -c "import industrial_collector._core as c; print('✅ OK'); print(c.Collector.instance().health())"
```

> Debug 编译（CLion 断点用）见 §7.2.3，改用 `-DCMAKE_BUILD_TYPE=Debug` 重新走一遍 CMake 步骤。

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
cd /home/meng/ibmcp

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

### 11.1 SSH / 网络 / 同步类

| 症状                        | 排查                                                                                                       |
|---------------------------------------------|------------------------------------------------------------------------------------------------------------|
| Windows 连不上欧拉（ping/SSH 都断） | `ping 192.168.80.134` 不通 → VMware 桥接/IP 问题；通但 SSH 不行 → 检查 `~/.ssh/config` 与免密         |
| CLion Remote Host 连不上    | 同上；Deployment SFTP 映射路径 `/home/meng/ibmcp/collector-cpp` 是否存在                                         |
| CLion 保存后欧拉侧 C++ 没更新 | Deployment → Options → Upload changed files automatically 是否设为 Always；排除路径是否误排了 `collector-cpp` |
| Windows 连不上 5432/6379    | 欧拉侧 `ss -tlnp \| grep 5432`；`docker ps` 看端口映射；防火墙 `systemctl status firewalld`（bootstrap 已关） |
| Windows 本地 FastAPI 起不来 | `.env` 是否在 `backend/` 下（`env_file=".env"` 相对路径）；`COLLECTOR_BACKEND` 是否误设 `native`       |
| 本地改了代码，欧拉部署版没变 | 正常现象：开发期不上传。部署需重新 `git pull` / 上传 + 重启 systemd（见 §13）                            |

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
| 前端页面 404                            | 确认 `/home/meng/ibmcp/frontend-dist/index.html` 存在（**Windows 侧** `npm run build` 后 scp 上传）；nginx.conf `root` 是否指向 `/home/meng/ibmcp/frontend-dist`                  |
| 前端 SPA 刷新 404                       | nginx.conf `location /` 必须有 `try_files $uri $uri/ /index.html`                                                                                        |
| WS 反代失败                             | nginx.conf WS location 必须加 `proxy_http_version 1.1` + `proxy_set_header Upgrade $http_upgrade` + `Connection upgrade` + 足够大的 `proxy_read_timeout` |

### 11.4 Python / FastAPI 类

| 症状                                                                 | 排查                                                                                                                           |
|----------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------|
| **Windows 上** `ModuleNotFoundError: industrial_collector._core`     | **正常现象**：Windows 没有 Linux `.so` → 把 `COLLECTOR_BACKEND` 改成 `stub`（§5A.3）                                           |
| **欧拉上** `ModuleNotFoundError: industrial_collector._core`         | `bash deployments/openeuler/build.sh` 是否成功；venv 解释器是否为 `/home/meng/ibmcp/venv/bin/python`                          |
| `RuntimeError: industrial_collector._core 未安装`                    | `COLLECTOR_BACKEND` 设成了 `native` 但没编译扩展 → 改 `stub` 或执行 build.sh                                                   |
| `ImportError: dynamic module does not define module export function` | **Python ABI 不匹配**：扩展用 3.11 编的却被 3.12 import → 确认 venv 用 python3.11                                              |
| PG 连接报错                                                          | Windows：`PG_HOST=192.168.80.134`；欧拉：`PG_HOST=127.0.0.1`。用户/库均为 `ibmcp`；PG 容器是否在跑                          |
| `asyncpg.exceptions.InvalidPasswordError`                            | Windows 本地 `.env` 里的 `PG_PASSWORD` 与 Docker 容器初始化密码不一致（`docker compose down -v` 会重置为 `ibmcp`）           |
| `ValueError: unknown collector backend: ffi`                         | `COLLECTOR_BACKEND` 只接受 `stub` 或 `native`（不是 `ffi`）                                                                    |

### 11.5 C++ 扩展类

| 症状                                           | 排查                                                                                                           |
|------------------------------------------------|----------------------------------------------------------------------------------------------------------------|
| CLion 断点 "No executable code found"          | 没带 `-g` → 用 `-DCMAKE_BUILD_TYPE=Debug` 重走 CMake 步骤（§7.2.3）                                              |
| `ptrace_scope` 报错（gdbserver 无法 attach）   | `echo 0 \| sudo tee /proc/sys/kernel/yama/ptrace_scope`                                                        |
| `libmodbus.so: cannot open shared object file` | `dnf install libmodbus-devel` 或 `echo "/usr/lib64" \| sudo tee /etc/ld.so.conf.d/ibmcp.conf && sudo ldconfig` |
| cmake 报 `CMAKE_CXX_STANDARD 17` 不支持        | gcc 版本太老 → `dnf update gcc gcc-c++`（openEuler 24.03 自带支持 C++17）                                      |

> **GIL 死锁排查**（`gdbserver attach` 后 continue 崩、扩展 shutdown 卡死、Python 持有 GIL 阻塞）→
> 根因与修复全部见 [05-绑定契约 GIL 规则表 / stop() 死锁修复](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md)。

### 11.6 前端类

| 症状                    | 排查                                                                                                        |
|-------------------------|-------------------------------------------------------------------------------------------------------------|
| `/api/v1/...` 404       | Vite proxy target 是否指向 `http://192.168.80.134:8000`（native 联调）或 `http://127.0.0.1:8000`（stub 本地） |
| WebSocket 连不上        | Vite proxy `/ws` 加 `ws: true`；target 口径要与 FastAPI 实际运行位置一致                                     |
| 前端报 `CORS`           | `vite.config.ts` 里 `server.proxy` 已代理 → 代理模式下浏览器到 Vite 同源，无 CORS                           |
| Vite 端口 5173 被占用   | `vite.config.ts` 设了 `strictPort: true` → 换端口或杀占用进程                                               |
| 部署到 Nginx 后页面空白 | Windows 侧 `npm run build` 是否成功；`dist\*` 是否已上传到 `/home/meng/ibmcp/frontend-dist/`；nginx.conf `root` 路径是否对；文件权限是否 `o+rX` |

---

## 十二、开发期资源消耗参考

### 12.1 欧拉虚拟机各服务资源占用

| 服务                         | 内存           | CPU       | 磁盘            | 备注                                    |
|------------------------------|----------------|-----------|-----------------|-----------------------------------------|
| PostgreSQL 15（Docker 容器） | 200~500 MB     | < 10%     | 按数据量增长    | 卷 `ibmcp_pg_data`                      |
| Redis 7（Docker 容器）       | 10~50 MB       | < 5%      | AOF 几十 MB     | 卷 `ibmcp_redis_data`                   |
| Node-RED（Docker 容器）      | 50~100 MB      | < 3%      | 流程配置几十 MB | 卷 `ibmcp_nodered_data`                 |
| nginx（裸机，仅部署后）      | < 10 MB        | < 1%      | —               | 反代 + 前端静态，空闲极轻               |
| uvicorn + 扩展（裸机 venv，`native` 联调时） | 150~300 MB | < 5%→20% | —          | 含采集线程池                            |
| Python venv（欧拉，编译宿主） | ~500 MB       | —         | —               | pip packages + pybind11                 |
| Docker 镜像                  | —              | —         | ~1 GB           | postgres:15 + redis:7 + nodered         |
| C++ 编译临时                 | —              | —         | ~2 GB           | `build/` 目录，debug 符号特别占空间     |
| 前端构建产物                 | —              | —         | ~20 MB          | `/home/meng/ibmcp/frontend-dist/`       |
| CLion Remote GDB             | ~100 MB        | < 5%      | ~500 MB         | Remote GDB Server（仅 C++ 调试时）      |
| **合计（典型开发态）**       | **0.9~1.5 GB** | **< 10%** | **< 5 GB**      | 采集压力大时内存上浮至 3.5 GB           |

> **对比旧架构**：PyCharm 远程解释器的辅助文件（`~/.cache/JetBrains/` 约 500 MB）不再落在欧拉——因为 PyCharm 现在全本地，
> 索引和缓存都在 Windows。欧拉侧只保留 CLion 的 GDB server。

### 12.2 欧拉侧磁盘增长预估

| 项目                 | 初始    | 日均增长       | 备注                                                            |
|----------------------|---------|----------------|-----------------------------------------------------------------|
| PG 数据              | —       | ~50~200 MB     | Docker 卷 `ibmcp_pg_data`，取决于点位数量 × 采样间隔 × 保留策略 |
| Docker 镜像          | ~1 GB   | —              | postgres:15 + redis:7 + nodered                                 |
| Python venv          | ~500 MB | —              | pip packages + pybind11                                         |
| C++ 编译临时         | ~2 GB   | 每次编译 +2 GB | `build/` 目录                                                   |
| 前端构建产物         | ~20 MB  | —              | `/home/meng/ibmcp/frontend-dist/`                               |

**建议**：`rm -rf /home/meng/ibmcp/collector-cpp/build/` 每次 Release 编译完清；PG 数据用
`docker exec ibmcp-postgres pg_dump` 定期备份。

### 12.3 Windows 侧磁盘占用（新增）

| 项目                  | 初始    | 备注                                                     |
|-----------------------|---------|----------------------------------------------------------|
| `.venv`（Windows）    | ~600 MB | 纯 Python 依赖，无 `.so`（平台无关）                     |
| `frontend/node_modules` | ~400 MB | npm 依赖                                                 |
| PyCharm 索引缓存      | ~500 MB | `%LOCALAPPDATA%\JetBrains\`（全本地，不进欧拉）           |
| CLion 索引缓存        | ~300 MB | 同上                                                     |
| **合计**              | ~1.8 GB | 只占 Windows 磁盘，与欧拉虚拟机容量无关                  |


---

## 十三、版本与变更记录

| 版本     | 日期       | 变更                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
|----------|------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **v8.0** | 2026-11-20 | **Python 环境口径统一（uv + 系统自带 python）**：① **Windows 侧 Python 改用 uv 管理**，解释器锁定 3.11.18（项目根与 `backend/.python-version` 双份一致），`uv sync` 按 `uv.lock` 同步依赖，`bootstrap.ps1` 重写为 uv 流程，废弃 `py -m venv` + `pip install -e .`；② **欧拉侧不单独装 Python、不装 uv**，用系统自带 `python3.11` 建 venv + pip，`bootstrap.sh` dnf 清单收紧为仅 nginx + docker + C++ 构建工具链（gcc/g++/cmake/make），移除 `python3`/`python3-devel`/`python3-pip`/`postgresql-devel`；③ **`build.sh` 新增 Python 3.11 版本校验 + `Python.h` 缺失时按需补装 `python3.11-devel`**（仅头文件，属编译工具链范畴，非装解释器）；④ §0 分工表 / §3.1 步骤表 / §5A Windows 侧 / §9 checklist 全文同步口径，§4.2 修正 pybind11 来源为 venv 内 pip 装（非系统 pybind11-devel） |
| **v7.0** | 2026-11-20 | **开发架构重构（Python 本地解释器 + C++ 欧拉编译 + 数据服务欧拉 Docker）**：① **Python 开发改回 Windows 本地解释器**（`py -3.11 -m venv .venv` + `pip install -e .`），PyCharm 用 `Add Local Interpreter`，**不再配 SSH 远程解释器**；② **`COLLECTOR_BACKEND` 分工固化**：Windows=`stub`（无 Linux `.so`）、欧拉=`native`；③ **Windows 本地 `.env` 直连欧拉数据服务**（`PG_HOST=192.168.80.134`、`REDIS_URL=redis://192.168.80.134:6379/0`），部署时改回 `127.0.0.1`；④ **§五 拆为 5-A（PyCharm 本地）/ 5-B（CLion Remote Host）/ 5-C（目录映射与纪律）**，原 §5.1 SSH 解释器、§5.2 CLion、§5.3 同步纪律全部重写；⑤ **Deployment 同步范围收窄**：只同步 `collector-cpp/`、`python/`、`deployments/`、`pyproject.toml`，**Python 源码开发期不上传**（部署时才上去）；⑥ **§五新增 5B.4 欧拉终端环境**（`dev-env.sh` 定位改为"C++ 编译/`native` 联调专用"）；⑦ **§六 隧道改为可选**（桥接网段直连即可，新增 6.3 连通性验证）；⑧ **§7.1 Python 调试改为本地**（`FastAPI (local, stub)` Run Config、无 Path mappings、零网络延迟）；⑨ **§7.2/§7.3 C++ 调试改为远程宿主视角**（`native` 起服务 + gdbserver 自动探测）；⑩ **§9 每日 checklist / 开发循环 / 联调阶段全部按新分工重写**；⑪ **§11.1 排查表重写**（新增"Windows 本地 FastAPI 起不来"、"本地改了欧拉部署版没变"）；⑫ **§12 资源表重写**（欧拉侧去掉 PyCharm 缓存，新增 12.3 Windows 侧占用） |
| **v7.1** | 2026-11-20 | **前端部署口径修正为「Windows 构建 + 上传静态产物」**：① 明确**欧拉不安装 Node.js / npm、不执行 `npm run build`**，`bootstrap.sh` 的 dnf 清单不含 nodejs（§0.1 分工表、§3.1 步骤表同步）；② **§8.2 重写**为「Windows `npm run build` → scp 上传 `frontend\dist\*` → 欧拉 `chmod -R o+rX` + `nginx -t && nginx -s reload`」三段式；③ **§8.3 从「备选：Vite dev server 跑在欧拉」改为「不推荐且默认不启用」**，给出 Node 依赖体积、HMR 跨网延迟、`VITE_API_TARGET` 已覆盖联调三条理由；④ §5-B Deployment 排除项把 `frontend/node_modules` + `frontend/dist` 合并为**整个 `frontend` 目录**（`dist` 走 scp 单独传，不走映射）；⑤ §9 部署期流程图与「前端更新流程」同步；§11.1 两条前端 404/空白的排查项更新 |
| **v6.2** | 2026-11-20 | **§5.1 全部对齐 PyCharm 2026.3 界面 + §5.1.3 扩写为详细操作指南**：① IDE 版本从 2024.2+ 改为 2026.3；② §5.1.1 SSH 解释器路径改为 `Ctrl+Alt+S → Python | Interpreter → Add Interpreter → On SSH`，补充 Existing/New SSH 配置选择 + Key pair 认证 + 右下角同步气泡（Sync and auto-upload files）；③ §5.1.2 Deployment 路径改为 `Ctrl+Alt+S → Build, Execution, Deployment | Deployment`（或 `Tools | Deployment | Configuration`），SFTP Connection 标签改用 SSH configuration 下拉，新增 "Use as Default" 步骤，Options 路径改为 `Tools | Deployment | Options`；④ §5.1.3 首次同步扩写为详细操作（Project 工具窗口 Alt+1 → 右键 → `Deployment | Upload to` → 选 euler-ibmcp → File Transfer 工具窗口监控进度）；⑤ 新增欧拉侧文件验证命令 + 自动同步验证（加注释 → Ctrl+S → grep）；⑥ Excluded Paths 补充具体路径配置；⑦ 新增常见问题排查（FTP/SFTP/WebDAV Connectivity 插件检查、Use as Default 等）；⑧ §5.2 CLion 同步对齐 2026.3 界面                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| **v6.1** | 2026-11-20 | **混合部署架构调整**：① **PG/Redis/Node-RED 从裸机 dnf 改为 Docker Compose**（新建 `deployments/openeuler/docker-compose.yml`，PG15 + Redis7 + Node-RED，数据卷持久化）；② **`bootstrap.sh` 重写为混合模式**（7 步：SELinux → dnf 裸机工具链 → 装 Docker → docker compose up → Nginx 配置 → uv → ptrace）；③ **前端部署到 Nginx 静态**（新增 §8.2 构建 + 部署到 `/home/meng/ibmcp/frontend-dist/` 流程 + nginx.conf 配置说明）；④ **Nginx 保持裸机**（托管前端静态 + 反代 FastAPI，不进 Docker）；⑤ **拓扑图更新**（新增 Docker Compose 层，裸机层只留 FastAPI/C++/Nginx）；⑥ **§6 端口表更新**（PG/Redis/Node-RED 标注 Docker 容器，新增 1880 端口）；⑦ **§10 Node-RED 从"可选"改为 docker-compose 内置**；⑧ **§11 排查新增 Docker 容器类 + Nginx 类**；⑨ **§12 资源消耗更新**（新增 Docker 镜像 + 前端构建产物）                                                                                                                                                                                                                                                                                                                                  |
| **v6.0** | 2026-11-20 | **重大架构调整 + 全面对齐实际代码**：① **远程开发模式从 JetBrains Gateway 改为 SSH 远程解释器 + Deployment 自动同步**（Windows 是代码源头，欧拉是自动同步镜像）；② **IP 从 `192.168.1.100` 改为 `192.168.80.134`**；③ **项目路径从 `02-IBMCP_Python` 改为 `02-IBMCP`**，欧拉路径从 `/home/<user>/01-IBMCP` 改为 `/home/meng/ibmcp`；④ **openEuler 版本从 22.03 LTS SP3 改为 24.03 LTS**（与 Dockerfile 基础镜像对齐）；⑤ **PG 口径从 `postgres/device_db` 无密码改为 `ibmcp/ibmcp/ibmcp`**（与 bootstrap.sh + .env.example + config.py 对齐）；⑥ **C++ 标准从 C++14 改为 C++17**（与 CMakeLists.txt 对齐）；⑦ **`COLLECTOR_BACKEND` 从 `ffi` 改为 `native`**（与 config.py 对齐）；⑧ **启动命令从 `app.main:create_app --factory` 改为 `app.main:app`**（与 systemd/Dockerfile/build.sh 对齐）；⑨ **冒烟测试 API 路径修正**（`from industrial_collector import Collector` → `NativeCollector` + `industrial_collector._core`）；⑩ **前端端口从 3000 改为 5173**（与 vite.config.ts 对齐）；⑪ **新增 §〇 核心问题解答**（欧拉是否需要完整代码）；⑫ 全文交叉引用路径 `02-IBMCP_Python` → `02-IBMCP`；⑬ SSH 开发用户明确为 `meng`，bootstrap 用 `root` |
| v5.1     | 2026-11-20 | Python 版本降级：源码编译从 3.11.9 改为 3.11.6                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| v5.0     | 2026-11-18 | 去重 + 对齐 01-07 文档口径                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| v4.0     | 2026-11-18 | C++ 标准升级（当时写 C++14，实际代码是 C++17）；Nginx 迁移到裸机                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| v3.0     | 2026-11-18 | PG15/Redis/Node-RED 从裸机迁移到 Docker Compose（v6.0 临时改回裸机，v6.1 恢复混合模式）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| v2.0     | 2026-11-18 | IDE 组合统一为 PyCharm + CLion                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| v1.0     | 2026-11-18 | 初版：所有服务裸机安装                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |