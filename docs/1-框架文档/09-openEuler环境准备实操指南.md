# IBMCP openEuler 环境准备实操指南

> 版本：v6.1　　编写日期：2026-11-20
>
> **v6.1 变更（2026-11-21）**：① Alembic 当前 head 为 `0003`，共建 **28 张业务表**（另含 `alembic_version`），并存在两套账号表（`sys_users/sys_roles` 在用，`users/roles/user_roles` 为 0001 遗留，见 §7.2）；② 成功响应统一包膜 `{code:0,message:"ok",data}`，HTTP 状态恒 200、错误用 `code` 表达（见 01 号 §11.3）；③ 关联新增 [12-文件代码结构及代码说明](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/12-文件代码结构及代码说明.md)，代码逐文件走读以 12 号为准。
>
> **文档分工**：本文是「从零搭环境 → 编译 → 部署 → 验收」的动手手册。
> **方案原理、Windows 开发、调试、日常工作流**见 [08-开发调试环境（欧拉VMware服务方案）](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/08-开发调试环境（欧拉VMware服务方案）.md)，本文不重复。
> **代码逐文件走读**见 [12-文件代码结构及代码说明](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/12-文件代码结构及代码说明.md)。
> 冲突时以仓库脚本 `deployments/`、`packaging/` 为准。
>
> 全程约定：欧拉 IP `192.168.80.135`、项目路径 `/home/meng/ibmcp`、SSH 用户 `meng`、root 仅用于首次 bootstrap。
> 你的 IP/网段不同时，全文替换即可。

## 0. 搭完应该得到什么

| 层 | 内容 |
|----|------|
| Docker 容器 | `ibmcp-postgres` :5432 · `ibmcp-redis` :6379 · `ibmcp-nodered` :1880 |
| 欧拉裸机 | 系统 python3.11 + `venv` · C++ 工具链 · `_core.so` · Nginx :80 · systemd 服务 |
| Windows | uv + `.venv`（3.11.18）· PyCharm 本地调试 · CLion 远程编译 · Vite |

```
Windows(PyCharm/CLion) --SSH/5432/6379--> openEuler(VM, 桥接)
                                           ├─ Docker: PG / Redis / Node-RED
                                           ├─ 裸机: venv + _core.so + Nginx + systemd
                                           └─ 现场 PLC（桥接网段）
```

## 1. VMware 创建虚拟机

### 1.1 镜像

| 项 | 值 |
|----|----|
| 版本 | openEuler 24.03 LTS x86_64（与 Dockerfile 基础镜像一致） |
| 下载 | https://www.openeuler.org/zh/download/ |
| 推荐 | `openEuler-24.03-LTS-x86_64-dvd.iso` |

```powershell
certutil -hashfile openEuler-24.03-LTS-x86_64-dvd.iso SHA256   # 与官网比对
```

### 1.2 新建

| 项 | 值 |
|----|----|
| 安装来源 | 稍后安装操作系统 |
| 客户机系统 | Linux → openEuler 24（找不到选 Other Linux 6.x 64-bit） |
| CPU / 内存 | ≥ 4 核 / ≥ 8 GB（推荐 16 GB） |
| 网络 | **桥接模式**（关键：要独立 IP、要访问 PLC 网段） |
| 磁盘 | 120 GB，单个文件 |

创建后挂载 ISO：编辑虚拟机设置 → 光驱 → 使用 ISO 镜像。

### 1.3 安装

| 步骤 | 值 |
|------|----|
| 语言 | 中文 |
| 软件选择 | **最小安装**（不要 GUI） |
| 安装位置 | 确认磁盘后完成 |
| 网络 | 打开网卡开关，主机名 `euler-ibmcp`，IPv4 先 DHCP |
| 用户 | root 密码；创建普通用户 `meng` |

安装完成后重启，登录执行：

```bash
ip addr show      # 记下 ens33/enp0s3 的 inet，例如 192.168.80.135
```

之后从 Windows SSH 操作：

```powershell
ssh meng@192.168.80.135
```

> ping 不通/SSH 失败：VMware → Virtual Network Editor → VMnet0 → 桥接到实际物理网卡（不要选“自动”）。

## 2. 首次配置（一次性）

### 2.1 SELinux / 防火墙 / 主机名 / 时区

```bash
sudo setenforce 0
sudo sed -i 's/^SELINUX=enforcing$/SELINUX=disabled/' /etc/selinux/config
sudo systemctl stop firewalld && sudo systemctl disable firewalld

sudo hostnamectl set-hostname euler-ibmcp
echo "192.168.80.135 euler-ibmcp" | sudo tee -a /etc/hosts
sudo timedatectl set-timezone Asia/Shanghai
sudo systemctl enable --now chronyd
```

> `bootstrap.sh` 第 1 步会再做一遍，幂等。

### 2.2 静态 IP（推荐）

```bash
nmcli connection show                     # 确认连接名，如 ens33
sudo nmcli connection modify "ens33" \
    ipv4.method manual \
    ipv4.addresses 192.168.80.135/24 \
    ipv4.gateway 192.168.80.2 \
    ipv4.dns "192.168.80.2 8.8.8.8"
sudo nmcli connection up "ens33"
ping -c 3 192.168.80.2
```

网关/DNS 按实际网段调整。

### 2.3 升级 + 基础工具

```bash
sudo dnf update -y
sudo dnf install -y git wget curl vim-enhanced net-tools chrony \
                    tar gzip zip unzip htop iotop lsof strace dnf-utils
sudo systemctl enable --now sshd
```

### 2.4 SSH 免密（Windows → 欧拉，root 或 meng）

把下面命令里的 `meng` 换成实际登录账号。当前开发机用 `root` 免密执行 bootstrap/部署，应用进程仍以 `meng` 运行。

```powershell
ssh-keygen -t ed25519 -C "ibmcp-dev" -f "$env:USERPROFILE\.ssh\ibmcp_euler"

type "$env:USERPROFILE\.ssh\ibmcp_euler.pub" | ssh meng@192.168.80.135 `
  "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"

ssh -i "$env:USERPROFILE\.ssh\ibmcp_euler" meng@192.168.80.135 "hostname"
```

`$env:USERPROFILE\.ssh\config` 追加：

```
Host euler-ibmcp
    HostName 192.168.80.135
    User meng
    Port 22
    IdentityFile ~/.ssh/ibmcp_euler
    ServerAliveInterval 30
    ServerAliveCountMax 3
```

### 2.5 软件源

全部使用在线源，不配置本地 ISO 源：

- 裸机包：openEuler 默认 dnf 源；
- Docker：`bootstrap.sh` 写入清华 `docker-ce.repo`（`DOCKER_REPO_MIRROR` 可覆盖）；
- Python：系统 python3.11 + pip，`PIP_INDEX` 默认清华（可覆盖）；
- Windows：uv 读 `pyproject.toml` 的清华 index。

内网环境把上述地址替换为内网镜像即可。

## 3. 同步项目代码

开发期欧拉只需要：`collector-cpp/`、`collector-python/`、`deployments/`、`packaging/`、`pyproject.toml`；
`backend/` 和 `frontend/` 仅部署/联调时需要。

**方式 A：scp（首次全量）**

```powershell
cd E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP
ssh meng@192.168.80.135 "mkdir -p /home/meng/ibmcp"
scp -r collector-cpp collector-python deployments packaging pyproject.toml `
    meng@192.168.80.135:/home/meng/ibmcp/
```

**方式 B：git**

```bash
mkdir -p /home/meng/ibmcp && cd /home/meng/ibmcp
git clone <仓库地址> .
```

**方式 C：CLion Deployment（日常 C++ 推荐）**

按 08 号 §4 配好 SFTP 后，右键项目根 → `Deployment | Upload to euler-ibmcp`。后续保存 C++ 自动上传。

验证：

```bash
ls /home/meng/ibmcp
# collector-cpp/ collector-python/ deployments/ packaging/ pyproject.toml
```

## 4. 一键 bootstrap

```bash
sudo bash /home/meng/ibmcp/deployments/openeuler/bootstrap.sh
```

| 步骤 | 内容 |
|------|------|
| 1/7 | 关闭 SELinux + 防火墙 |
| 2/7 | dnf 装 `gcc gcc-c++ cmake make pkgconf-pkg-config nginx` + 运维工具；尝试装可选驱动头文件 `mosquitto-devel`/`libmodbus-devel`/`open62541-devel`（失败跳过） |
| 3/7 | 装 Docker CE + compose v2 插件，启动 docker，当前用户加入 docker 组 |
| 4/7 | `docker compose up -d` 起 PG15 / Redis7 / Node-RED，等待健康检查 |
| 5/7 | 部署 `packaging/nginx/ibmcp.conf` → `/etc/nginx/conf.d/`，创建 `frontend-dist`，启动 Nginx |
| 6/7 | 校验系统自带 python3.11 与 `venv` 模块（不装 uv、不装 python3-pip） |
| 7/7 | `ptrace_scope=0` 持久化（CLion gdbserver 需要） |

> 脚本幂等，可重复执行。不会安装 Node.js/npm，前端在 Windows 构建。

验证：

```bash
cd /home/meng/ibmcp
docker compose -f deployments/openeuler/docker-compose.yml ps    # 三个 Up (healthy)
docker exec ibmcp-postgres pg_isready -U ibmcp -d ibmcp
docker exec ibmcp-redis redis-cli ping
curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:1880   # 200
systemctl is-active nginx
python3 --version                                                # 3.11.x
gcc --version | head -1 && cmake --version | head -1
```

### 4.1 Docker Hub 拉取失败（国内网络常见）

`bootstrap.sh` 只装 Docker，不配镜像源。若 `docker compose up -d` 报 `connection refused`，写入镜像并重启：

```bash
sudo tee /etc/docker/daemon.json >/dev/null <<'EOF'
{
  "registry-mirrors": [
    "https://docker.1panel.live",
    "https://docker.m.daocloud.io",
    "https://docker.1ms.run"
  ]
}
EOF
sudo systemctl daemon-reload && sudo systemctl restart docker
```

再执行 `docker compose -f deployments/openeuler/docker-compose.yml up -d`。镜像站可用性会变化，失效时换其他加速地址。

## 5. 组件速查

### 5.1 PostgreSQL 15（Docker）

- 容器 `ibmcp-postgres`，镜像 `postgres:15`，端口 `5432:5432`，卷 `ibmcp_pg_data`，时区 `Asia/Shanghai`。
- 口径固定：用户/密码/库均为 `ibmcp`；不区分旧口径 `postgres/device_db`。
- 建表**只走 Alembic**（§7.2），不在容器里挂 init SQL。

```bash
docker exec -it ibmcp-postgres psql -U ibmcp -d ibmcp
PGPASSWORD=ibmcp psql -U ibmcp -h 127.0.0.1 -d ibmcp -c "\dt"
```

### 5.2 Redis 7（Docker）

- 容器 `ibmcp-redis`，端口 `6379:6379`，AOF 开启，卷 `ibmcp_redis_data`。
- URL：欧拉 `redis://127.0.0.1:6379/0`；Windows `redis://192.168.80.135:6379/0`。
- ⚠️ 容器已部署且 `.env` 已配置，但当前后端代码**尚未使用 Redis**（缓存/限流/在线态待接入，见 12 号 §9）。

### 5.3 Node-RED（Docker）

- 容器 `ibmcp-nodered`，端口 `1880:1880`，卷 `ibmcp_nodered_data`。
- 用途：PLC 模拟器、MQTT broker、协议探针、数据导出。
- 访问 `http://192.168.80.135:1880`。

### 5.4 Nginx（裸机）

配置源文件 `packaging/nginx/ibmcp.conf`，要点：

- `root /home/meng/ibmcp/frontend-dist`（只托管构建产物）；
- `/api/v1/ws/collect` 必须放在 `/api/` 之前，带 `Upgrade`/`Connection` 头与 `proxy_read_timeout 3600s`；
- `location / { try_files $uri $uri/ /index.html; }` 支持 Vue Router history；
- 改配置或换前端后：`sudo nginx -t && sudo nginx -s reload`。

### 5.5 C++ 工具链

| 项 | 要求 |
|----|------|
| gcc / g++ | openEuler 24.03 自带，支持 C++17 |
| cmake | ≥ 3.20 |
| C++ 标准 | **C++17**（CMakeLists 强制） |
| pybind11 | venv 内 `pip install pybind11`，CMake 通过 `pybind11_DIR` 定位 |
| Python.h | `build.sh` 检测缺失时补装 `python3-devel`（openEuler 24.03 包名，仅头文件） |
| 可选驱动 | `libmodbus` / `libmosquitto` / `open62541(1.4+)`；CMake `QUIET`，缺谁跳过谁 |

openEuler 24.03 SP3 仓库无 `libmodbus-devel` / `open62541-devel`，需要时源码编译；装到 `/usr/local` 后执行：

```bash
echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/ibmcp.conf
sudo ldconfig
```

验证：

```bash
gcc --version | head -1
cmake --version | head -1
echo 'auto x = 42;' | g++ -std=c++17 -x c++ - -o /dev/null && echo "C++17 OK"
```

### 5.6 Python 边界（重要）

| 侧 | 方式 | 原因 |
|----|------|------|
| Windows | uv 管理 3.11.18 | 不污染系统、按 `uv.lock` 精确同步 |
| openEuler | 系统 python3.11 + `venv` + pip | `_core.so` 必须与宿主解释器 ABI 精确对齐 |

**欧拉不要装 uv、不要用 uv 建 Python**，否则 `_core.so` 链接的解释器与运行时不一致，会报
`ImportError: dynamic module does not define module export function`。

## 6. 编译 C++ 扩展

```bash
cd /home/meng/ibmcp
bash deployments/openeuler/build.sh
```

脚本依次做 4 件事：

1. 用系统 `python3`（3.11）建 `venv/`；
2. 幂等装 `cmake/gcc-c++/make`，按需补 `python3-devel`；
3. venv 内装 pybind11 → CMake 编译 `_core.so` → `cmake --install --prefix collector-python`；
4. `pip install .` 把 `industrial_collector`（含 `.so`）与 backend 装进 venv。

内网镜像：`PIP_INDEX=http://<内网>/simple bash deployments/openeuler/build.sh`。

冒烟：

```bash
cd /home/meng/ibmcp && source venv/bin/activate
python -c "import industrial_collector._core as c; print(c.Collector.instance().health())"
```

产物位置：

| 产物 | 路径 |
|------|------|
| 构建目录 | `/home/meng/ibmcp/collector-cpp/build/`（可删） |
| 落位 `.so` | `collector-python/industrial_collector/_core*.so` |
| venv 内 `.so` | `venv/lib/python3.11/site-packages/industrial_collector/` |

## 7. 部署

### 7.1 同步完整代码（含 backend）

```powershell
scp -r "E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\backend" `
    meng@192.168.80.135:/home/meng/ibmcp/
```

### 7.2 `.env` + Alembic

```bash
cd /home/meng/ibmcp/backend
cp .env.example .env
```

欧拉侧关键项：

| 项 | 值 |
|----|----|
| `APP_ENV` / `APP_HOST` / `APP_PORT` | `production` / `0.0.0.0` / `8000` |
| `PG_HOST` | `127.0.0.1` |
| `PG_PORT` / `PG_USER` / `PG_PASSWORD` / `PG_DATABASE` | `5432` / `ibmcp` / `ibmcp` / `ibmcp` |
| `REDIS_URL` | `redis://127.0.0.1:6379/0` |
| `COLLECTOR_BACKEND` | **`native`** |

```bash
cd /home/meng/ibmcp && source venv/bin/activate
cd backend && alembic upgrade head
alembic current            # 确认 head = 0003
PGPASSWORD=ibmcp psql -U ibmcp -h 127.0.0.1 -d ibmcp -c "\dt"
# 28 张业务表：base_*(10) / running_*(5) / using_*(4) / time_series_data
# / control_commands / runtime_plan_revision / audit_config_change
# / sys_users / sys_roles / users / roles / user_roles（后三者为 0001 遗留）
```

> 迁移幂等；表结构以 [02-数据模型](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/02-数据模型与字段契约（FastAPI版）.md) 为准。
> `0002` 迁移会创建 `sys_users` / `sys_roles` 并预置 `admin`；初始密码见交付记录，首次登录后立即修改。
> ⚠️ 账号以 `sys_users` / `sys_roles` 为准；`users` / `roles` / `user_roles` 是 `0001` 的历史遗留，未被 `auth.py` 使用，后续迁移会清理。

### 7.3 前端：Windows 构建 → 上传

```powershell
cd E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\frontend
npm install
npm run build

ssh meng@192.168.80.135 "rm -rf /home/meng/ibmcp/frontend-dist/*"
scp -r dist\* meng@192.168.80.135:/home/meng/ibmcp/frontend-dist/
```

```bash
chmod -R o+rX /home/meng/ibmcp/frontend-dist
sudo nginx -t && sudo nginx -s reload
```

> 只传 `dist\*`，不传源码 / `node_modules/`。Nginx 需要穿越 `/home/meng`，bootstrap 已放开 `o+x`。

### 7.4 启动 FastAPI

联调期（前台看日志）：

```bash
cd /home/meng/ibmcp && source deployments/openeuler/dev-env.sh
ibmcp-api-native
```

部署期（systemd 开机自启）：

```bash
sudo cp /home/meng/ibmcp/packaging/systemd/ibmcp-api.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now ibmcp-api
systemctl status ibmcp-api
journalctl -u ibmcp-api -f
```

服务要点：`User=meng`、`WorkingDirectory=.../backend`、`EnvironmentFile=.env`、
`venv/bin/uvicorn app.main:app --host 0.0.0.0 --port 8000 --workers 1`、`Restart=always`。

### 7.5 部署后访问

| 地址 | 期望 |
|------|------|
| `http://192.168.80.135/` | 前端首页 |
| `http://192.168.80.135/api/v1/docs` | OpenAPI |
| `http://192.168.80.135:1880` | Node-RED |
| 刷新 `/devices` 等子路由 | 不 404 |

## 8. 验收清单

### 8.1 欧拉侧

| # | 检查 | 期望 |
|---|------|------|
| 1 | `docker --version && docker compose version` | 27.x / v2.x |
| 2 | `docker compose -f deployments/openeuler/docker-compose.yml ps` | 三容器 Up (healthy) |
| 3 | `docker exec ibmcp-postgres pg_isready -U ibmcp -d ibmcp` | accepting connections |
| 4 | `docker exec ibmcp-redis redis-cli ping` | PONG |
| 5 | `curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:1880` | 200 |
| 6 | `systemctl is-active nginx` | active |
| 7 | `python3 --version` | 3.11.x |
| 8 | `source venv/bin/activate && python -c "from industrial_collector import _core"` | 不抛错 |
| 9 | `cat /proc/sys/kernel/yama/ptrace_scope` | 0 |
| 10 | `alembic upgrade head` 后 `\dt` | 表齐全 |

### 8.2 Windows → 欧拉

| 目标 | 命令 | 期望 |
|------|------|------|
| SSH 22 | `ssh meng@192.168.80.135 "hostname"` | euler-ibmcp |
| PG 5432 | `Test-NetConnection 192.168.80.135 -Port 5432` | True |
| Redis 6379 | `Test-NetConnection 192.168.80.135 -Port 6379` | True |
| Node-RED 1880 | `(Invoke-WebRequest http://192.168.80.135:1880 -UseBasicParsing).StatusCode` | 200 |
| Nginx 80 | `(Invoke-WebRequest http://192.168.80.135 -UseBasicParsing).StatusCode` | 200（部署后） |
| 本机 stub 8000 | `(Invoke-WebRequest http://127.0.0.1:8000/api/v1/docs -UseBasicParsing).StatusCode` | 200（启动后） |
| gdbserver 1234 | `Test-NetConnection 192.168.80.135 -Port 1234` | True（调试启动后） |

### 8.3 端到端

前端首页 → 子路由刷新不 404 → OpenAPI 可交互 → native 采集时 WS 101 → `time_series_data` 有数据。

## 9. 重启与自启

```
□ 虚拟机开机 → Docker 容器随 docker 自启（restart: unless-stopped）
   未起：cd /home/meng/ibmcp && docker compose -f deployments/openeuler/docker-compose.yml up -d
□ systemctl status nginx       → active
□ systemctl status ibmcp-api   → active（未安装见 §7.4）
□ 浏览器验证 http://192.168.80.135/ 与 /api/v1/docs
```

开发期每日清单见 08 号 §8.1。

## 10. 故障排查（搭建 / 部署类）

### 10.1 VM / SSH

| 现象 | 修复 |
|------|------|
| ping 不通 / SSH 连不上 | VMware 桥接绑错网卡；Virtual Network Editor → VMnet0 选实际物理网卡 |
| 防火墙未关 | `sudo systemctl stop firewalld && sudo systemctl disable firewalld` |
| IP 被 DHCP 改掉 | 按 §2.2 配静态 IP |

### 10.2 Docker

| 现象 | 修复 |
|------|------|
| 拉镜像超时 / `pull access denied` | 配 `/etc/docker/daemon.json` 的 `registry-mirrors`，重启 docker |
| `docker-ce.repo` 安装失败 | 确认清华 baseurl；内网用 `DOCKER_REPO_MIRROR` |
| 容器 restart 循环 | `docker compose -f ... logs <服务>`；PG 多为卷权限或端口占用 |
| `docker compose` 不存在 | 装的是 compose v1；需 `docker-compose-plugin` 并使用 `docker compose` |
| 端口占用 | `ss -tlnp \| grep 5432` |
| 想重置 PG | `docker compose down -v`（⚠️ 丢数据） |

### 10.3 build.sh

| 报错 | 修复 |
|------|------|
| `需要 Python 3.11` | `PYTHON_BIN=python3.11 bash build.sh` |
| `Python.h: No such file` | `sudo dnf install -y python3-devel` |
| `pybind11Config.cmake not found` | `source venv/bin/activate && pip install pybind11` 后重跑 |
| `dynamic module does not define module export function` | 欧拉误用了 uv 建的 Python；删掉后改用系统 python3.11 重建 |
| `cannot open shared object file: libmodbus.so` | 源码库装到 `/usr/local` 后执行 `ldconfig` |
| C++17 / cmake 版本报错 | 升级 cmake（≥3.20）；确认 gcc 支持 C++17 |

### 10.4 Nginx / 前端

| 现象 | 原因 / 修复 |
|------|-------------|
| `/` 403 | `sudo chmod o+x /home/meng /home/meng/ibmcp && chmod -R o+rX .../frontend-dist && sudo nginx -s reload` |
| `/` 404 / 欢迎页 | `frontend-dist/index.html` 不存在或 `root` 指错 |
| 子路由刷新 404 | 缺 `try_files $uri $uri/ /index.html` |
| WS 握手失败 | `/api/v1/ws/collect` 顺序与 Upgrade 头；`nginx -t && nginx -s reload` |
| `/api/` 502 | FastAPI 没在 8000：`systemctl status ibmcp-api` |
| 改了配置不生效 | 必须 `reload`，不是 `restart` |

日志：`sudo tail -f /var/log/nginx/error.log`。

### 10.5 Windows 连 PG / Redis

| 现象 | 修复 |
|------|------|
| Connection refused / 超时 | 欧拉防火墙、容器端口映射 `0.0.0.0:5432->5432` |
| `password authentication failed` | `.env` 的 `PG_PASSWORD` 与 compose 一致（`ibmcp`） |
| ping 通但端口不通 | 先在欧拉 `psql` 本机验证，再查 Windows 安全软件 |

### 10.6 ptrace / `_core`

| 现象 | 修复 |
|------|------|
| `ptrace: Operation not permitted` | `echo 0 \| sudo tee /proc/sys/kernel/yama/ptrace_scope` 并写 `/etc/sysctl.conf` |
| Windows 报 `_core` 导入失败 | `COLLECTOR_BACKEND=stub`（Windows 无 Linux `.so`） |

## 附：dev-env.sh

```bash
source /home/meng/ibmcp/deployments/openeuler/dev-env.sh   # 必须 source
echo 'source /home/meng/ibmcp/deployments/openeuler/dev-env.sh' >> ~/.bashrc
```

| 别名 | 作用 |
|------|------|
| `ibmcp-build` | `bash deployments/openeuler/build.sh` |
| `ibmcp-api` | 按 `.env` 的 backend 起 uvicorn |
| `ibmcp-api-native` | 强制 `COLLECTOR_BACKEND=native` 起 uvicorn |
| `ibmcp-migrate` | `alembic upgrade head` |
| `ibmcp-test` | `pytest -q backend/tests` |
| `ibmcp-up` / `ibmcp-down` / `ibmcp-logs` | Docker Compose 操作 |

> 脚本对 scp/sftp 等非交互会话完全静默，避免 `Received message too long`。改动后自检：`bash -lc 'true' | wc -c` 应为 `0`。
> 不要用 PowerShell 管道把脚本内容喂给远端 `cat`（编码/换行会损坏），传文件用 `scp`。

## 版本记录

| 版本 | 日期 | 变更 |
|------|------|------|
| v6.0 | 2026-11-20 | 与 08 号去重拆分：本文只保留从零搭建、bootstrap、编译、部署、验收、环境类排查；Windows 开发/调试/工作流移入 08；统一 IP `192.168.80.135`、`collector-python/` 路径与脚本口径一致 |
| v5.0 | 2026-11-20 | 增加组件配置参考、uv 边界、编译/迁移/前端部署/验收全流程 |
