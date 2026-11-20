# IBMCP openEuler 环境准备实操指南

>
版本：v3.1　　编写日期：2026-11-20　　对应方案：[08-开发调试环境（欧拉VMware服务方案）](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/08-开发调试环境（欧拉VMware服务方案）.md)
>
> **本文档定位**：给"第一次搭环境"的同学准备的**动手手册**，全程复制粘贴即可跑通。与 08 号架构方案（v6.1 混合模式）
> **完全对齐**，是 08 号的"操作手册版"。
>
>
**配套引用**：[01-架构设计](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/01-IBMCP-FastAPI重构架构设计.md) · [02-数据模型](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/02-数据模型与字段契约（FastAPI版）.md) · [05-绑定契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md) · [07-前端契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/07-Vue3前端架构与多端交付契约.md)

---

## 环境架构速览（一句话版）

> **PG/Redis/Node-RED 走 Docker Compose，FastAPI/C++ 扩展/Nginx 裸机跑；Windows 11 只做编辑器和浏览器。**

| 层                      | 方式                        | 包含                                                                                                            |
|-------------------------|-----------------------------|-----------------------------------------------------------------------------------------------------------------|
| **Docker Compose 容器** | `docker compose up -d`      | PostgreSQL 15 · Redis 7 · Node-RED                                                                              |
| **欧拉裸机**            | dnf 安装                    | Python 3.11 + venv · gcc-c++（C++17） · cmake · libmodbus/open62541 · FastAPI (uvicorn) · Nginx 反代 + 前端静态 |
| **Windows 11**          | SSH 远程解释器 + Deployment | PyCharm Professional（Python+前端+DB） · CLion（C+++gdb） · Chrome                                              |

> **为什么 FastAPI/C++ 不也放进 Docker？** —— pybind11 扩展（`_core.so`）必须用宿主机器的 Python 编译，且要访问宿主的
> libmodbus.so / open62541.so 和现场 PLC 网段。详见 08 号 §1.3。

> **远程开发模式**：SSH 远程解释器 + Deployment 自动同步（**非** JetBrains Gateway）。代码源头在 Windows（
> `E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP`），欧拉侧 `/home/meng/ibmcp` 是 PyCharm 自动同步的镜像副本。详见 08 号 §〇。

---

## 第一部分：VMware 创建 openEuler 虚拟机

### 1.1 下载 openEuler 镜像

| 项目     | 值                                                                                                                |
|----------|-------------------------------------------------------------------------------------------------------------------|
| 版本     | **openEuler 24.03 LTS**（x86_64）· 与 `packaging/docker/Dockerfile` 基础镜像 `openeuler/openeuler:24.03-lts` 一致 |
| 下载页   | https://www.openeuler.org/zh/download/                                                                            |
| 推荐镜像 | `openEuler-24.03-LTS-x86_64-dvd.iso`（包含完整 dnf 包）                                                           |

```powershell
# Windows PowerShell 校验 iso SHA256（可选但推荐）
certutil -hashfile openEuler-24.03-LTS-x86_64-dvd.iso SHA256
# 对比官网给出的 SHA256 值
```

### 1.2 VMware 新建虚拟机

打开 VMware Workstation → **文件 → 新建虚拟机 → 自定义（高级）**：

| #  | 步骤           | 值                                   | 说明                                                                                 |
|----|----------------|--------------------------------------|--------------------------------------------------------------------------------------|
| 1  | 硬件兼容性     | Workstation 17.x                     | 用你当前版本                                                                         |
| 2  | 安装来源       | **稍后安装操作系统**                 | 先创建空虚拟机，后面挂载 iso                                                         |
| 3  | 客户机操作系统 | Linux → **openEuler 24**             | 找不到选 "Other Linux 6.x kernel 64-bit"                                             |
| 4  | 虚拟机名称     | `ibmcp-openeuler`                    | 随意                                                                                 |
| 5  | 处理器         | **4 核**                             | C++ 编译吃 CPU，最少 4 核                                                            |
| 6  | 内存           | **8 GB（最少）** / **16 GB（推荐）** | 3 个容器 + FastAPI + Nginx 足够                                                      |
| 7  | 网络类型       | **桥接模式**                         | ⚠️ 关键！让虚拟机直接获取局域网 IP，Windows 能 SSH 进去，虚拟机也能访问现场 PLC 网段 |
| 8  | I/O 控制器     | LSI Logic                            | 默认即可                                                                             |
| 9  | 磁盘类型       | SCSI                                 | 默认即可                                                                             |
| 10 | 磁盘           | **120 GB**，单个文件                 | 系统 + Docker 镜像 + PG 数据卷 + C++ 编译临时 + venv + 前端构建                      |

创建完成后 → **编辑虚拟机设置 → 光驱 → 使用 ISO 镜像文件 → 选中刚下载的 iso → 确定**。

### 1.3 安装 openEuler（最小安装）

启动虚拟机 → 看到 openEuler 安装菜单 → **回车**选 "Test this media & install openEuler 24.03-LTS"。

图形安装界面（VMware 窗口显示不全时按 **Tab** 在选项间跳转）：

| #            | 操作                                                                                                   |
|--------------|--------------------------------------------------------------------------------------------------------|
| 语言         | 中文 → 继续                                                                                            |
| **软件选择** | **最小安装（Minimal Install）** —— 纯命令行，不要选带 GUI 的任何选项！省空间、启动快                   |
| 安装位置     | 点进去确认磁盘已识别 → 完成                                                                            |
| **网络**     | ⚠️ **重要**：点进去打开网卡开关（右上角 ON/OFF），设置主机名 `euler-ibmcp` → 应用；IPv4 配置 DHCP 即可 |
| 用户设置     | root 密码设一个你记得住的；**创建普通用户 `meng`**（开发期 SSH/PyCharm 用这个用户）                    |
| 开始安装     | 等待 ~10 分钟                                                                                          |
| 重启         | 完成后重启                                                                                             |

> **用户规划**：root 用于首次 bootstrap（装系统包、Docker、Nginx），`meng` 用于日常 SSH / PyCharm 远程解释器 / CLion Remote
> Host。与 08 号 §2.3 对齐。

### 1.4 拿到 IP，然后永远 SSH 操作

登录进虚拟机（root 或 meng）后，**只做这一件事**：

```bash
# 查看 IP 地址（桥接模式应该拿到和 Windows 同网段的 IP）
ip addr show
# 找到 ens33 / enp0s3 之类网卡的 inet 行，记下 IP（比如 192.168.80.134）
```

然后**回到 Windows 主机**，用 SSH 连过去：

```powershell
# Windows PowerShell / CMD
ssh meng@192.168.80.134
# 输入刚才设的密码 → 以后所有操作都在这个 SSH 窗口里做
```

> 如果 `ping` 不通或 SSH 连不上 → VMware 桥接模式没生效。检查：VMware → Virtual Network Editor → VMnet0 → **桥接到**
> 改成你实际在用的物理网卡（不要选"自动"）。

---

## 第二部分：虚拟机首次配置（一次性）

SSH 登进去后，把下面全部复制粘贴执行。

### 2.1 SELinux + 防火墙 + 主机名 + 时区

```bash
# —— 以下需要 root 权限，用 sudo 或直接用 root 执行 ——

# SELinux 关闭
sudo setenforce 0
sudo sed -i 's/^SELINUX=enforcing$/SELINUX=disabled/' /etc/selinux/config

# 防火墙关闭（开发环境）
sudo systemctl stop firewalld
sudo systemctl disable firewalld

# 主机名 + 时区
sudo hostnamectl set-hostname euler-ibmcp
echo "192.168.80.134 euler-ibmcp" | sudo tee -a /etc/hosts
sudo timedatectl set-timezone Asia/Shanghai
sudo systemctl enable --now chronyd
chronyc sources
```

### 2.2 静态 IP（推荐，避免 DHCP 换 IP）

如果想固定 IP 为 `192.168.80.134`：

```bash
nmcli connection show                                        # 看连接名，比如 "ens33"
sudo nmcli connection modify "ens33" \
    ipv4.method manual \
    ipv4.addresses 192.168.80.134/24 \
    ipv4.gateway 192.168.80.2 \
    ipv4.dns "192.168.80.2 8.8.8.8"
sudo nmcli connection up "ens33"
ping -c 3 192.168.80.2 && ping -c 3 8.8.8.8
```

> **网关和 DNS 根据你的实际网段调整**。上面的 `192.168.80.2` 是示例，你的可能不同。

### 2.3 ⭐ 配置本地 ISO dnf 源（所有 dnf 操作之前必做！）

openEuler 最小安装后默认的 `.repo` 要么指向已断开的 VMware 光驱、要么指向公网镜像，第一次跑 `dnf install` 大概率直接报错。本节
**完全离线配好本地源**，后续所有 dnf 操作都走这个本地源。

> 📖
> 本节内容整合了实战踩坑经验。原始踩坑记录见：[openEuler本地ISO源实战指南.md](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/openEuler本地ISO源实战指南.md)

#### 2.3.1 确认 ISO 在哪里

```bash
# 情况 A：ISO 通过 VMware 光驱挂载着（安装完没断光驱，最常见）
lsblk | grep -E 'sr|rom'     # 看到 sr0 / sr1 之类
ls -la /dev/cdrom

# 情况 B：ISO 文件已经拷到虚拟机磁盘
find / -name "*openEuler*iso" -type f 2>/dev/null
```

#### 2.3.2 挂载光驱

```bash
sudo mkdir -p /mnt/cdrom

# 情况 A：用 VMware 光驱（/dev/cdrom == /dev/sr0）
sudo mount /dev/cdrom /mnt/cdrom
# 如果 /dev/cdrom 不存在，改用：sudo mount /dev/sr0 /mnt/cdrom

# 情况 B：磁盘上的 ISO 文件 loop 挂载（把路径改成你 find 出来的实际路径）
# sudo mount -o loop /home/openEuler-24.03-LTS-x86_64-dvd.iso /mnt/cdrom

# 验证挂载成功（repodata 必须存在！这是 dnf 识别源的关键）
ls /mnt/cdrom/repodata/
# 预期：里面有 repomd.xml、*.xml.gz 等索引文件，没有就说明挂错了
```

#### 2.3.3 ⭐ 关键优化：复制到本地硬盘

> **为什么要复制？** 虚拟光驱有时会出现不稳定的底层问题（如诡异的 `Bad file:// URL` 读取失败）。将光盘内容复制到硬盘后，读取速度极快且稳定可靠。——
> 来自实战踩坑

```bash
sudo mkdir -p /data/everything
sudo cp -r /mnt/cdrom/* /data/everything/

# 验证复制完成
ls /data/everything/repodata/   # 必须看到 repomd.xml
```

#### 2.3.4 ⚠️ repodata 不在？用 find 精准定位

如果复制完 `ls /data/everything/repodata/` 报 "No such file or directory"，说明 ISO 目录结构可能多了一层：

```bash
sudo find /data/ -type d -name repodata
```

根据输出结果，**把 baseurl 修正到 repodata 的上一级目录**。例如：

- `find` 输出 `/data/everything/repodata` → baseurl 写 `file:///data/everything` ✅
- `find` 输出 `/data/everything/openEuler/repodata` → baseurl 写 `file:///data/everything/openEuler` ✅

> ⚠️ **baseurl 绝对不能指向 repodata 本身**，必须是 repodata 的父目录。

#### 2.3.5 备份旧 repo → 写入本地源配置

> **隐藏字符陷阱**：不要手动 `vi` 编辑 repo 文件，复制粘贴长路径极易混入中文标点或不可见空格。**一律用 heredoc
> 一次性写入**。

```bash
sudo mkdir -p /etc/yum.repos.d/backup
sudo mv /etc/yum.repos.d/*.repo /etc/yum.repos.d/backup/ 2>/dev/null || true

# ⚠️ 如果 2.3.4 find 出的 repodata 实际路径不是 /data/everything，
#   把下面 baseurl 里的 /data/everything 改成你 find 出来的父目录！

sudo tee /etc/yum.repos.d/local-iso.repo << 'REPO_EOF'
[local-data]
name=Local Everything Data
baseurl=file:///data/everything
enabled=1
gpgcheck=0
REPO_EOF

ls -la /etc/yum.repos.d/local-iso.repo
```

#### 2.3.6 刷新缓存 + 验证

```bash
sudo dnf clean all
sudo dnf makecache
# 预期：最后几行显示 Metadata caching done，没有 repomd.xml 错误

sudo dnf repolist
# 预期：repo id 显示 local-data，status enabled，packages 数量 ~5000+
```

#### 2.3.7 （可选）装 dnf-utils → 管理在线源

本地 ISO 里没有的包（Docker 可能在 EPOL 源里），可以临时启用公网源：

```bash
sudo dnf install -y dnf-utils

# 临时启用某个备份的公网源
# sudo yum-config-manager --enable update
# 安装完后立刻禁用，保持本地源优先
# sudo yum-config-manager --disable update
```

#### 2.3.8 本地 ISO 源常用命令清单

| 操作               | 命令                                                |
|--------------------|-----------------------------------------------------|
| 挂载光驱           | `sudo mount /dev/cdrom /mnt/cdrom`                  |
| 复制到硬盘         | `sudo cp -r /mnt/cdrom/* /data/everything/`         |
| 定位索引目录       | `sudo find /data/ -type d -name repodata`           |
| 清理缓存           | `sudo dnf clean all`                                |
| 刷新源             | `sudo dnf makecache`                                |
| 查看启用的源       | `sudo dnf repolist`                                 |
| 临时只用本地源安装 | `sudo dnf install <包名> -y --disablerepo="update"` |
| 查包在不在         | `sudo dnf search <关键字> --disablerepo="update"`   |
| 禁用公网源         | `sudo yum-config-manager --disable update`          |
| 启用公网源         | `sudo yum-config-manager --enable update`           |

### 2.4 升级系统 + 装基础工具

```bash
sudo dnf update -y
sudo dnf install -y git wget curl vim-enhanced net-tools \
               chrony tar gzip zip unzip \
               htop iotop lsof strace openssh-server dnf-utils
sudo systemctl enable --now sshd
```

### 2.5 Windows 侧 SSH 免密登录（用户 meng）

```powershell
# —— 在 Windows PowerShell 执行 ——

# 1) 生成密钥（如果已有可跳过）
ssh-keygen -t ed25519 -C "ibmcp-dev" -f "$env:USERPROFILE\.ssh\ibmcp_euler"

# 2) 推送公钥到欧拉虚拟机（用户 meng）
type "$env:USERPROFILE\.ssh\ibmcp_euler.pub" | ssh -p 22 meng@192.168.80.134 `
    "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"

# 3) 验证（应该不再提示输入密码）
ssh -i "$env:USERPROFILE\.ssh\ibmcp_euler" meng@192.168.80.134 "hostname"

# 4) 写入 Windows SSH config（PyCharm 远程解释器 / CLion 自动识别）
# 编辑 $env:USERPROFILE\.ssh\config，加入：
# Host euler-ibmcp
#     HostName 192.168.80.134
#     User meng
#     Port 22
#     IdentityFile ~/.ssh/ibmcp_euler
#     ServerAliveInterval 30
#     ServerAliveCountMax 3
```

---

## 第三部分：一键 bootstrap（Docker + 裸机 + Nginx）

### 3.1 同步项目代码到欧拉（首次）

在执行 bootstrap 之前，需要先把项目代码同步一次到欧拉的 `/home/meng/ibmcp`。有三种方式，**推荐方式 B**：

#### 方式 A：scp 直接拷贝（在 Windows PowerShell 执行）

```bash
# —— 在 Windows PowerShell 执行 ——
scp -r "E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\*" meng@192.168.80.134:/tmp/ibmcp-upload/
# 然后在欧拉上：
ssh meng@192.168.80.134 "sudo mkdir -p /home/meng/ibmcp && sudo cp -r /tmp/ibmcp-upload/* /home/meng/ibmcp/ && sudo chown -R meng:meng /home/meng/ibmcp"
```

> 适合：还没装 PyCharm、或想快速验证。缺点：后续改代码还是要手动 scp。

#### 方式 B：PyCharm Deployment 全量上传（推荐）

**前提**：PyCharm 已按 §5.1 配好 SSH 远程解释器和 Deployment（SFTP → euler-ibmcp，Root path `/home/meng/ibmcp`）。

**第 1 步：确保欧拉侧目录就绪**

```bash
# 在欧拉上执行（只需一次）
sudo mkdir -p /home/meng/ibmcp && sudo chown meng:meng /home/meng/ibmcp
```

**第 2 步：在 PyCharm 中发起全量上传**

```
1. 打开 Project 工具窗口（Alt+1）→ 右键项目根节点 "02-IBMCP"
2. 右键菜单 → Deployment | Upload to → 选 "euler-ibmcp"
   ※ 如果 euler-ibmcp 已设为默认服务器，菜单直接显示 "Upload to euler-ibmcp"，点它即可
3. 底部弹出 File Transfer 工具窗口，实时显示传输文件列表
4. 等待完成（1~3 分钟），File Transfer 窗口显示统计："N files uploaded"
```

**第 3 步：验证文件已到位**

```bash
# 在欧拉上执行
ls /home/meng/ibmcp/
# 应看到：backend/  frontend/  collector-cpp/  python/  deployments/  packaging/  pyproject.toml

ls /home/meng/ibmcp/backend/app/main.py              # FastAPI 入口
ls /home/meng/ibmcp/collector-cpp/CMakeLists.txt     # C++ 构建
ls /home/meng/ibmcp/deployments/openeuler/bootstrap.sh

# 确认排除项没传上去
ls /home/meng/ibmcp/venv/                    # 应不存在
ls /home/meng/ibmcp/frontend/node_modules/   # 应不存在
```

**第 4 步：确认自动同步生效**

```
1. PyCharm 打开 backend/app/core/config.py
2. 加一行注释 # sync test → Ctrl+S
3. File Transfer 工具窗口应出现一条新传输记录：config.py → euler-ibmcp
4. 欧拉侧验证：grep "sync test" /home/meng/ibmcp/backend/app/core/config.py
5. 能匹配 → 自动同步正常 → 删掉测试注释 → Ctrl+S
```

> **详细图文步骤和常见问题排查**：见 08 号 §5.1.3。
>
> **排除项配置**（防止传了不该传的文件）：
> `Ctrl+Alt+S → Build, Execution, Deployment | Deployment | Options → Excluded Paths → +`
> - `.venv/`、`venv/`（Windows venv 不传）
> - `frontend/node_modules/`、`frontend/dist/`
> - `__pycache__/`、`.ruff_cache/`
> - `build/`、`ibmcp.egg-info/`
> - `.git/`（git 操作只在 Windows 做）

#### 方式 C：git clone（如果有远程仓库）

```bash
sudo mkdir -p /home/meng/ibmcp && sudo chown meng:meng /home/meng/ibmcp
cd /home/meng/ibmcp && git clone <你的仓库地址> .
```

> 适合：项目已推到远程仓库。缺点：后续还是要配 PyCharm Deployment 做自动同步。

> **首次同步后，后续 PyCharm Deployment 会自动保持同步**（保存即 upload），不需要重复手动操作。详见 08 号 §〇 和 §5.1.3。

### 3.2 执行 bootstrap.sh（一键完成 7 步）

项目自带 `deployments/openeuler/bootstrap.sh`，**一条命令完成所有环境初始化**：

```bash
# —— 在欧拉虚拟机上执行（需要 root）——
sudo bash /home/meng/ibmcp/deployments/openeuler/bootstrap.sh
```

脚本自动完成以下 **7 步**（与 08 号 §3.1 完全对齐）：

| 步骤 | 内容                                                                                                                                                                                               |
|------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1/7  | 关闭 SELinux + 防火墙（已在 §2.1 做过的话会跳过）                                                                                                                                                  |
| 2/7  | dnf 安装裸机编译工具链：gcc/g++/cmake/python3-devel/postgresql-devel/pybind11-devel/libmodbus-devel/mosquitto-devel/open62541-devel/nginx/git/wget/curl 等（**不装 postgresql/redis，走 Docker**） |
| 3/7  | 安装 Docker Engine + docker compose 插件，启动 docker 守护进程，当前用户加入 docker 组                                                                                                             |
| 4/7  | `docker compose up -d` 启动 PG15 + Redis 7 + Node-RED 三个容器（用 `deployments/openeuler/docker-compose.yml`），等待健康检查通过                                                                  |
| 5/7  | 部署 Nginx 配置（`packaging/nginx/ibmcp.conf` → `/etc/nginx/conf.d/ibmcp.conf`），创建前端静态目录 `/home/meng/ibmcp/frontend-dist`，启动 Nginx                                                          |
| 6/7  | 安装 uv（Python 包管理器）                                                                                                                                                                         |
| 7/7  | 放开 ptrace_scope（CLion gdbserver 远程调试需要）                                                                                                                                                  |

> **为什么要用 bootstrap.sh 而不是手动一步步做？** 脚本是幂等的（可重复执行），且与 08 号架构方案完全对齐。手动做容易遗漏步骤或口径不一致。

### 3.3 docker-compose.yml 说明

`deployments/openeuler/docker-compose.yml` 定义了三个容器：

| 容器名           | 镜像                      | 端口        | 数据卷               | 说明                                        |
|------------------|---------------------------|-------------|----------------------|---------------------------------------------|
| `ibmcp-postgres` | `postgres:15`             | `5432:5432` | `ibmcp_pg_data`      | PG 用户 `ibmcp` / 密码 `ibmcp` / 库 `ibmcp` |
| `ibmcp-redis`    | `redis:7`                 | `6379:6379` | `ibmcp_redis_data`   | AOF 持久化                                  |
| `ibmcp-nodered`  | `nodered/node-red:latest` | `1880:1880` | `ibmcp_nodered_data` | PLC 模拟器/协议探针                         |

> **PG 口径与代码完全一致**：`POSTGRES_USER=ibmcp`、`POSTGRES_PASSWORD=ibmcp`、`POSTGRES_DB=ibmcp`，与
> `backend/.env.example` 和 `backend/app/core/config.py` 对齐。不要用 `postgres/device_db`（旧版口径，已废弃）。

### 3.4 bootstrap 完成后的验证

```bash
# Docker 容器状态
cd /home/meng/ibmcp
docker compose -f deployments/openeuler/docker-compose.yml ps
# 应看到 ibmcp-postgres / ibmcp-redis / ibmcp-nodered 三个容器 Up

# PG 验证（容器内）
docker exec ibmcp-postgres pg_isready -U ibmcp -d ibmcp
# 或从宿主用密码连接（FastAPI 的连法）
PGPASSWORD=ibmcp psql -U ibmcp -h 127.0.0.1 -d ibmcp -c "SELECT 1;"

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

## 第四部分：编译 C++ 扩展 + venv + 前端部署

### 4.1 Step 1：使用 build.sh 创建 venv + 编译 C++ 扩展

`deployments/openeuler/build.sh` 负责创建 venv + 编译 C++ 扩展 + 安装 backend：

```bash
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

# 编译并安装 industrial_collector（含 _core.so）+ backend
pip install ./python ./backend
```

> **openEuler 24.03 自带 Python 3.11**，不需要源码编译。bootstrap.sh 的 dnf 步骤已装好 `python3-devel`。

### 4.2 Step 2：C++ 编译链说明

| 组件         | 版本要求             | 说明                                                                 |
|--------------|----------------------|----------------------------------------------------------------------|
| gcc-c++      | openEuler 24.03 自带 | 支持 C++17                                                           |
| cmake        | ≥ 3.20               | `CMakeLists.txt` 要求 `cmake_minimum_required(VERSION 3.20)`         |
| **C++ 标准** | **C++17**            | `collector-cpp/CMakeLists.txt` 第 5 行：`set(CMAKE_CXX_STANDARD 17)` |
| pybind11     | pybind11-devel       | `find_package(pybind11 CONFIG REQUIRED)`                             |
| libmodbus    | libmodbus-devel      | `pkg_check_modules(MODBUS QUIET libmodbus)`                          |
| mosquitto    | mosquitto-devel      | `pkg_check_modules(MOSQUITTO QUIET libmosquitto)`                    |
| open62541    | open62541-devel      | `pkg_check_modules(OPEN62541 QUIET open62541)`（需 1.4+）            |

验证编译链：

```bash
echo "gcc:       $(gcc --version | head -1)"
echo "g++:       $(g++ --version | head -1)"
echo "cmake:     $(cmake --version | head -1)"

# C++17 特性验证
echo 'auto x = 42; constexpr int y = []{ return 7; }(); static_assert(x > y, "");' | \
    g++ -std=c++17 -x c++ - -o /dev/null && echo "✅ C++17 OK"

echo "modbus:    $(pkg-config --modversion libmodbus 2>/dev/null || echo 'NOT FOUND')"
echo "mosquitto: $(pkg-config --modversion libmosquitto 2>/dev/null || echo 'NOT FOUND')"
echo "open62541: $(rpm -q open62541-devel 2>/dev/null || echo 'NOT FOUND')"
```

> **C++ 标准是 C++17，不是 C++14**。`CMakeLists.txt` 明确设置 `CMAKE_CXX_STANDARD 17`。

### 4.3 Step 3：冒烟测试

编译完成后，验证 `_core.so` 扩展能加载：

```bash
cd /home/meng/ibmcp
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

> **注意 API 路径**：代码里**没有** `from industrial_collector import Collector`。正确路径是：
> - C++ 扩展模块：`industrial_collector._core`（pybind11 编译产出）
> - Python 封装层：`app.core.collector.ffi.NativeCollector`（`backend/app/core/collector/ffi.py`）
> - 接口协议：`app.core.collector.protocol.CollectorApi`

> **常见编译失败排查**：
>
> | 错误                                         | 解决                                                                                  |
> |----------------------------------------------|---------------------------------------------------------------------------------------|
> | `pybind11 not found`                         | `dnf install -y pybind11-devel`；venv 里 `pip install pybind11`                       |
> | `modbus.h: No such file`                     | `dnf install -y libmodbus-devel`                                                      |
> | `open62541.h: No such file`                  | `dnf install -y open62541-devel`（需 1.4+）                                           |
> | `Python3 not found`                          | `dnf install -y python3-devel`                                                        |
> | `libopen62541.so: cannot open shared object` | `echo "/usr/local/lib" \| sudo tee /etc/ld.so.conf.d/open62541.conf && sudo ldconfig` |

### 4.4 Step 4：配置 `.env` + Alembic 迁移

```bash
cd /home/meng/ibmcp/backend
cp .env.example .env

# 确认 .env 内容（PG 口径已与 Docker 容器对齐）：
#   PG_HOST=127.0.0.1
#   PG_PORT=5432
#   PG_USER=ibmcp
#   PG_PASSWORD=ibmcp
#   PG_DATABASE=ibmcp
#   REDIS_URL=redis://127.0.0.1:6379/0
#   COLLECTOR_BACKEND=stub  ← 开发期先用 stub，联调时改 native

source /home/meng/ibmcp/venv/bin/activate
alembic upgrade head
# 输出：Running upgrade -> 0001_initial_schema, done

PGPASSWORD=ibmcp psql -U ibmcp -h 127.0.0.1 -d ibmcp -c "\dt"
# 应该看到建好的表
```

> **COLLECTOR_BACKEND 取值**：`stub`（纯 Python，无硬件）或 `native`（加载 `_core.so`，真实采集）。**不是 `ffi`**。`config.py` 第
> 29 行：`collector_backend: str = Field(default="stub", description="stub | native")`。

> 完整建表（`base_*`/`running_*`/`time_series_data` 等表）**不走 Docker init**，而是用 Alembic
> 迁移。建表的字段、类型、约束、索引全部以 [02-数据模型文档](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/02-数据模型与字段契约（FastAPI版）.md)
> 为唯一权威。

### 4.5 Step 5：前端构建并部署到 Nginx

```bash
cd /home/meng/ibmcp/frontend

# 安装前端依赖（首次）
npm install

# 构建（产出 dist/ 目录）
npm run build

# 部署到 Nginx 静态目录（bootstrap.sh 已创建 /home/meng/ibmcp/frontend-dist）
cp -r dist/* /home/meng/ibmcp/frontend-dist/

# 重载 Nginx（无中断）
sudo nginx -t && sudo nginx -s reload
```

部署后的访问入口：

```
http://192.168.80.134           → 前端（Nginx 静态）
http://192.168.80.134/api/v1/*  → FastAPI（Nginx 反代）
http://192.168.80.134:1880      → Node-RED（Docker）
```

### 4.6 Step 6：手动启动 FastAPI 验证全链路

```bash
cd /home/meng/ibmcp/backend
source /home/meng/ibmcp/venv/bin/activate

uvicorn app.main:app --host 0.0.0.0 --port 8000
```

看到 `INFO: Uvicorn running on http://0.0.0.0:8000` 就成功了。**不要关掉这个终端**（或者加 `&` 放后台）。

> **启动命令是 `app.main:app`**（不是 `app.main:create_app --factory`）。`backend/app/main.py` 同时定义了 `create_app()`
> 函数和模块级 `app = create_app()`，systemd service 和 Dockerfile 都用 `app.main:app`。

**Windows 主机验证**（另开 PowerShell）：

```powershell
# FastAPI Swagger 文档（直连）
curl http://192.168.80.134:8000/api/v1/docs

# 通过 Nginx 反代验证
curl http://192.168.80.134/api/v1/docs

# 前端页面（Nginx 静态）
curl http://192.168.80.134
```

> **uvicorn 报错排查**：
>
> | 错误                                              | 排查                                                                                                       |
> |---------------------------------------------------|------------------------------------------------------------------------------------------------------------|
> | `No module named 'app'`                           | 必须在 `backend/` 目录下跑（Working directory: `/home/meng/ibmcp/backend`）                                      |
> | `No module named 'industrial_collector._core'`    | venv 激活了吗？`pip install ./python ./backend` 成功了吗？                                                 |
> | `RuntimeError: industrial_collector._core 未安装` | `COLLECTOR_BACKEND=native` 但没编译扩展 → 改 `COLLECTOR_BACKEND=stub` 先跑通 Python 侧                     |
> | PG 连接报错                                       | 确认 `PG_HOST=127.0.0.1` `PG_USER=ibmcp` `PG_PASSWORD=ibmcp`（不是 `postgres/device_db`）；PG 容器在跑吗？ |
> | `ValueError: unknown collector backend: ffi`      | `COLLECTOR_BACKEND` 只接受 `stub` 或 `native`（不是 `ffi`）                                                |

---

## 第五部分：Windows 远程开发配置

> 详见 08 号 §五，这里只保留关键配置和命令。

### 5.1 PyCharm Professional（SSH 远程解释器 + Deployment）

```
# —— 配置 SSH 远程解释器（非 Gateway）——
Ctrl+Alt+S → Python | Interpreter → Add Interpreter → On SSH
步骤 1：选 "Existing" → 选已配好的 SSH 配置 euler-ibmcp
        或选 "New" → Host: 192.168.80.134 / Port: 22 / Username: meng → Key pair 认证
步骤 2：Interpreter path: /home/meng/ibmcp/venv/bin/python3.11
步骤 3：Sync folder:
        Local path:  E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP
        Remote path: /home/meng/ibmcp
步骤 4：右下角弹出同步提示 → 点 "Sync and auto-upload files"

# —— 配置 Deployment（自动同步）——
Ctrl+Alt+S → Build, Execution, Deployment | Deployment → + → SFTP → "euler-ibmcp"
Connection 标签 → SSH configuration: 选 euler-ibmcp → Root path: /home/meng/ibmcp
Mappings 标签 → Local path: E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP
                Deployment path: /
选中 euler-ibmcp → 点 "Use as Default"
Options → Upload changed files automatically to the default server: Always
          Delete remote files when local are deleted: ✅

# —— 数据库工具连 Docker PG/Redis ——
Database 工具窗口 → + → PostgreSQL → Host: 192.168.80.134, Port: 5432, DB: ibmcp, User: ibmcp, Password: ibmcp
Database 工具窗口 → + → Redis → Host: 192.168.80.134, Port: 6379
```

> **SSH 远程解释器 vs Gateway**：SSH 远程解释器在 Windows 本地建索引，只在运行/调试时连欧拉的解释器。每次保存 Deployment 自动
> upload。详见 08 号 §5.1.1。

### 5.2 CLion（C++ Remote Host + gdb）

```
Ctrl+Alt+S → Build, Execution, Deployment | Toolchains → + → Remote Host → 选 euler-ibmcp
CMake executable: /usr/bin/cmake
C++ Compiler:     /usr/bin/g++
Debugger:         /usr/bin/gdb

Ctrl+Alt+S → Build, Execution, Deployment | Deployment → + → SFTP → euler-ibmcp
    Mappings → Local path:        E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\collector-cpp
               Deployment path:    /home/meng/ibmcp/collector-cpp
    Options → Upload changed files automatically to the default server: Always
              勾选 Never overwrite local files from deployment server

Ctrl+Alt+S → Build, Execution, Deployment | CMake → + → Debug-Remote
    Toolchain: Remote Host
    Build directory: /home/meng/ibmcp/build/cmake-build-debug
```

### 5.3 SSH 端口转发

**方式 A**：PyCharm / CLion 自动转发（推荐，连接时自动建隧道）

**方式 B**：写进 `~/.ssh/config`：

```
Host euler-ibmcp
    HostName 192.168.80.134
    User meng
    Port 22
    IdentityFile ~/.ssh/ibmcp_euler
    ServerAliveInterval 30

    LocalForward 5432 localhost:5432    # PG (Docker 容器)
    LocalForward 6379 localhost:6379    # Redis (Docker 容器)
    LocalForward 1880 localhost:1880    # Node-RED (Docker 容器)
    LocalForward 8000 localhost:8000    # FastAPI (裸机)
    LocalForward 5678  localhost:5678   # debugpy (裸机)
    LocalForward 1234  localhost:1234   # gdbserver (裸机)
```

### 5.4 隧道验证

```powershell
# Windows PowerShell
foreach ($p in 5432, 6379, 1880, 8000, 5678, 1234) {
    $ok = Test-NetConnection -ComputerName 127.0.0.1 -Port $p -WarningAction SilentlyContinue
    Write-Host "Port $p : $($ok.TcpTestSucceeded)"
}
```

### 5.5 端口总览

| 端口 | 服务                  | 跑在哪                     |
|------|-----------------------|----------------------------|
| 22   | SSH                   | 欧拉宿主                   |
| 5432 | PostgreSQL 15         | **Docker 容器** → 宿主映射 |
| 6379 | Redis 7               | **Docker 容器** → 宿主映射 |
| 1880 | Node-RED              | **Docker 容器** → 宿主映射 |
| 8000 | FastAPI               | **欧拉裸机** venv          |
| 80   | Nginx 反代 + 前端静态 | **欧拉裸机**               |
| 5678 | debugpy               | **欧拉裸机**               |
| 1234 | gdbserver             | **欧拉裸机**               |

---

## 第六部分：快速验证清单

所有步骤跑完后，一段脚本确认全链路：

```bash
echo "========== 系统 =========="
cat /etc/openEuler-release | head -1
echo "Python: $(python3 --version)"
echo "Docker: $(docker --version)"

echo ""
echo "========== Docker 容器 =========="
cd /home/meng/ibmcp && docker compose -f deployments/openeuler/docker-compose.yml ps

echo ""
echo "========== Nginx =========="
systemctl is-active nginx

echo ""
echo "========== PostgreSQL =========="
docker exec ibmcp-postgres pg_isready -U ibmcp -d ibmcp
PGPASSWORD=ibmcp psql -U ibmcp -h 127.0.0.1 -d ibmcp -t -c \
    "SELECT count(*) FROM information_schema.tables WHERE table_schema='public';" | awk '{print "表数量:", $0}'

echo ""
echo "========== Redis =========="
docker exec ibmcp-redis redis-cli ping

echo ""
echo "========== Node-RED =========="
curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:1880

echo ""
echo "========== FastAPI =========="
curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8000/api/v1/docs 2>&1 || echo "(uvicorn 可能没启动)"

echo ""
echo "========== C++ 扩展 =========="
source /home/meng/ibmcp/venv/bin/activate
python -c "import industrial_collector._core as c; print('✅ _core loaded:', c.health())" \
    2>&1 || echo "⚠️ _core 不可用（stub 模式正常）"

echo ""
echo "========== 前端 =========="
ls /home/meng/ibmcp/frontend-dist/index.html 2>/dev/null && echo "✅ 前端已部署" || echo "⚠️ 前端未部署（npm run build + cp dist/*）"

echo ""
echo "========== Alembic =========="
cd /home/meng/ibmcp/backend && alembic current 2>&1
```

Windows 侧验证：

```powershell
# PowerShell 一键验证
$urls = @(
    "http://192.168.80.134/api/v1/docs",   # FastAPI（Nginx 反代）
    "http://192.168.80.134:8000/api/v1/docs", # FastAPI（直连）
    "http://192.168.80.134",               # 前端（Nginx 静态）
    "http://192.168.80.134:1880"           # Node-RED
)
foreach ($u in $urls) {
    try {
        $r = Invoke-WebRequest -Uri $u -TimeoutSec 5 -UseBasicParsing
        Write-Host "$u → $($r.StatusCode)"
    } catch {
        Write-Host "$u → ❌ $($_.Exception.Message)"
    }
}
```

---

## 第七部分：日常启动 checklist

每天开发前跑一遍：

```
□ VMware → 启动 openEuler 虚拟机 → 确认 IP 192.168.80.134 能 ping 通
□ Docker 容器：cd /home/meng/ibmcp && docker compose -f deployments/openeuler/docker-compose.yml ps
    → ibmcp-postgres / ibmcp-redis / ibmcp-nodered 全部 Up
□ 裸机服务：systemctl status nginx → active
□ 欧拉侧 venv：source /home/meng/ibmcp/venv/bin/activate → python --version
□ PyCharm → 打开 02-IBMCP → 确认远程解释器已连（状态栏显示 SSH: euler-ibmcp）
□ PyCharm → 确认 Deployment 自动同步开启
□ CLion → Open Recent → Remote Host 自动连上
□ 隧道验证：Test-NetConnection 5432/6379/1880/8000
□ 浏览器：
    http://192.168.80.134         → 前端（Nginx 静态，已部署时）
    http://192.168.80.134/api/v1/docs → FastAPI OpenAPI（Nginx 反代）
    http://192.168.80.134:8000/api/v1/docs → FastAPI（直连）
    http://192.168.80.134:1880    → Node-RED（Docker）
□ PyCharm Database → ibmcp → 表结构正常
```

---

## 第八部分：常见问题速查

### dnf / 本地 ISO 源

>
完整排错见 [openEuler本地ISO源实战指南.md](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/openEuler本地ISO源实战指南.md)

| 症状                                           | 排查                                                                                                             |
|------------------------------------------------|------------------------------------------------------------------------------------------------------------------|
| `dnf install` 报 `Cannot open/read repomd.xml` | ISO 没挂载好 → `mount` 看 `/mnt/cdrom`；复制到硬盘了吗？`ls /data/everything/repodata/`                          |
| `dnf` 报 `Bad file:// URL`                     | **隐藏字符陷阱！** 手动编辑 repo 文件混入中文标点或不可见空格 → **删掉重来，用 heredoc 一次性写入**（见 §2.3.5） |
| 复制到硬盘后找不到 repodata                    | `find /data/ -type d -name repodata` 精准定位，baseurl 改到 repodata 的**上一级目录**                            |
| `No matching Packages`（但确定 ISO 里应该有）  | 包名写错？先用 `dnf search <关键字>` 搜；或这个包确实只在公网源里（如 Docker）→ 临时启用公网                     |
| 重启后 dnf 又报找不到源                        | VMware 光驱"开机连接"勾上了吗？或改用 `/data/everything` 硬盘路径（复制到硬盘是稳定方案）                        |
| dnf 卡 5 分钟以上                              | 有被 enable 的公网源连不上 → `dnf repolist` 看 enabled 状态 → 非本地源全部 enabled=0                             |
| repo 文件名跟命令里写的不一样                  | 操作前先 `ls /etc/yum.repos.d/` 核对文件名                                                                       |

### VMware / 网络

| 症状                    | 排查                                                                                                                                                                                     |
|-------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Windows ping 不通虚拟机 | VMware → Virtual Network Editor → VMnet0 → **桥接到** 你的物理网卡（不是"自动"）                                                                                                         |
| 虚拟机拿不到 IP         | 手动设：`nmcli con show` 看连接名 → `nmcli con mod "ens33" ipv4.method manual ipv4.addresses 192.168.80.134/24 ipv4.gateway 192.168.80.2 ipv4.dns 192.168.80.2` → `nmcli con up "ens33"` |
| SSH 连不上              | `ss -tlnp \| grep 22`；`systemctl status sshd`                                                                                                                                           |

### Docker / 容器

| 症状                               | 排查                                                                                                                |
|------------------------------------|---------------------------------------------------------------------------------------------------------------------|
| `docker compose up -d` 报错        | `docker compose -f deployments/openeuler/docker-compose.yml config` 检查 YAML；`docker compose logs` 看哪个容器先崩 |
| PG 容器启动后立刻退出              | 大概率数据卷里有旧版本数据 → `docker compose down -v` 清卷重来（**慎用**，会删所有数据）                            |
| 想进容器手动 SQL                   | `docker exec -it ibmcp-postgres psql -U ibmcp -d ibmcp`                                                             |
| `docker compose down` 后数据丢了？ | **不应该**。`down` 只删容器，数据卷保留。`down -v` 才会删卷                                                         |
| Docker 守护进程没跑                | `systemctl status docker`；`sudo systemctl start docker`                                                            |
| `docker compose` 命令不存在        | openEuler 24.03 装的是 `docker-compose-plugin`，命令是 `docker compose`（无连字符）                                 |

### Nginx

| 症状                   | 排查                                                                                                                                                     |
|------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------|
| Nginx 502 反代 FastAPI | 宿主 `curl http://127.0.0.1:8000` 测 → FastAPI 是否在跑                                                                                                  |
| Nginx 改配置没生效     | `sudo nginx -t && sudo nginx -s reload`（不要 systemctl restart）                                                                                        |
| WS 断开                | nginx.conf WS location 必须加 `proxy_http_version 1.1` + `proxy_set_header Upgrade $http_upgrade` + `Connection upgrade` + 足够大的 `proxy_read_timeout` |
| 前端页面 404           | 确认 `/home/meng/ibmcp/frontend-dist/index.html` 存在（`npm run build` + `cp dist/*`）                                                                         |
| 前端 SPA 刷新 404      | nginx.conf `location /` 必须有 `try_files $uri $uri/ /index.html`                                                                                        |

### Python / FastAPI

| 症状                                                                 | 排查                                                                                                                           |
|----------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------|
| `ModuleNotFoundError: industrial_collector._core`                    | venv 激活了吗？`pip install ./python ./backend` 成功了吗？                                                                     |
| `RuntimeError: industrial_collector._core 未安装`                    | `COLLECTOR_BACKEND=native` 但没编译扩展 → 改 `stub` 或执行 build.sh                                                            |
| `ImportError: dynamic module does not define module export function` | **Python ABI 不匹配**：3.11 编的扩展被 3.12 import。确认 venv 用 python3.11                                                    |
| PG 连接报错                                                          | 确认 `PG_HOST=127.0.0.1` `PG_USER=ibmcp` `PG_PASSWORD=ibmcp` `PG_DATABASE=ibmcp`（不是 `postgres/device_db`）；PG 容器在跑吗？ |
| `ValueError: unknown collector backend: ffi`                         | `COLLECTOR_BACKEND` 只接受 `stub` 或 `native`（不是 `ffi`）                                                                    |

### C++ 扩展

| 症状                                      | 排查                                                                                                       |
|-------------------------------------------|------------------------------------------------------------------------------------------------------------|
| `No executable code found`（CLion 断点）  | 没带 `-g` → `CMAKE_BUILD_TYPE=Debug pip install ./python ./backend --no-build-isolation`                   |
| `ptrace_scope` 报错（gdbserver）          | `echo 0 \| sudo tee /proc/sys/kernel/yama/ptrace_scope`（bootstrap.sh 已自动执行）                         |
| `libmodbus.so: cannot open shared object` | `dnf install libmodbus-devel`；或 `echo "/usr/lib64" \| sudo tee /etc/ld.so.conf.d/ibmcp.conf && ldconfig` |
| cmake 报 `CMAKE_CXX_STANDARD 17` 不支持   | gcc 版本太老 → `dnf update gcc gcc-c++`（openEuler 24.03 自带支持 C++17）                                  |

### 磁盘清理

```bash
# 每月一次：清理悬空 Docker 镜像/网络/构建缓存（保留数据卷）
docker system prune -f

# 每次 Release 编译完清 C++ 临时文件
rm -rf /home/meng/ibmcp/build/

# 清 PyCharm 远程缓存
rm -rf ~/.cache/JetBrains/
```

---

## 版本与变更记录

| 版本     | 日期       | 变更                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
|----------|------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **v3.1** | 2026-11-20 | **§3.1 + §5.1 全部对齐 PyCharm 2026.3 界面**：① 方式 B（PyCharm Deployment）从 2 行注释扩写为 4 步详细操作（确保目录就绪 → Project 工具窗口 Alt+1 → 右键 `Deployment | Upload to` → File Transfer 窗口监控 → 验证文件到位 → 确认自动同步）；② 方式 A（scp）补充 `chown` 和 `ssh` 远程执行命令；③ §5.1/§5.2 菜单路径全部改为 `Ctrl+Alt+S → Build, Execution, Deployment | Deployment`（2026.3 界面），SFTP 用 SSH configuration 下拉，新增 Use as Default 步骤；④ 新增 Excluded Paths 排除项配置（`Ctrl+Alt+S → Build, Execution, Deployment | Deployment | Options → Excluded Paths`）；⑤ 新增自动同步验证步骤（加注释 → Ctrl+S → grep 验证）；对应 08 号 v6.2                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| **v3.0** | 2026-11-20 | **全面对齐 08 号 v6.1 混合模式架构**：① **openEuler 版本从 22.03 LTS SP3 改为 24.03 LTS**（与 Dockerfile 对齐）；② **IP 从 `192.168.1.100` 改为 `192.168.80.134`**；③ **项目路径从 `02-IBMCP_Python` 改为 `02-IBMCP`**，欧拉路径从 `~/01-IBMCP` 改为 `/home/meng/ibmcp`；④ **远程开发模式从 Gateway 改为 SSH 远程解释器 + Deployment**（Windows 是代码源头）；⑤ **PG 口径从 `postgres/device_db` 无密码改为 `ibmcp/ibmcp/ibmcp`**；⑥ **C++ 标准从 C++14 改为 C++17**；⑦ **`COLLECTOR_BACKEND` 从 `ffi` 改为 `native`/`stub`**；⑧ **uvicorn 启动命令从 `create_app --factory` 改为 `app.main:app`**；⑨ **冒烟测试从 `from industrial_collector import Collector` 改为 `NativeCollector` + `industrial_collector._core`**；⑩ **Python 从源码编译 3.11.6 改为 openEuler 24.03 自带 python3.11**（删除编译步骤）；⑪ **docker-compose.yml 从内联改为引用项目自带 `deployments/openeuler/docker-compose.yml`**；⑫ **Nginx 从 8080 自定义配置改为 80 端口 + `packaging/nginx/ibmcp.conf` + 前端静态部署**；⑬ **SSH 用户从 root 改为 meng（开发）/ root（bootstrap）**；⑭ **新增前端构建部署步骤**（npm build → `/home/meng/ibmcp/frontend-dist/`）；⑮ **引用 bootstrap.sh 一键脚本**（7 步），删除手动逐步安装；⑯ 全文交叉引用路径 `02-IBMCP_Python` → `02-IBMCP` |
| v2.3     | 2026-11-20 | Python 版本降级：源码编译从 3.11.9 改为 3.11.6（v3.0 已改为系统自带，不再编译）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| v2.2     | 2026-11-20 | 整合实战踩坑指南（本地 ISO 源 9 小节）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| v2.0     | 2026-11-20 | 与 08 号架构方案对齐的完整重写                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| v1.0     | 2026-11-19 | 初版                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |