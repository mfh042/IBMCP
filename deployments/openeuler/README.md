# deployments/openeuler — 文件说明

本目录是 IBMCP（C++/Drogon 架构）在 **openEuler 侧**的全部部署与开发脚本。开发在 Windows 11，构建/运行/部署在 openEuler（当前目标机 `192.168.80.136`）。

所有脚本默认约定的两个根目录：

| 变量 | 默认值 | 含义 |
|---|---|---|
| `PROJECT_ROOT` / `EULER_ROOT` | `/home/meng/ibmcp` | 项目源码根 |
| `IBMCP_BUILD_DIR` / `EULER_BUILD` | `/home/meng/ibmbuild` | C++ 库/驱动构建产物根（drogon/trantor/libmodbus/open62541 + env.sh） |

---

## 1. 文件清单与职责

共 10 个文件，按用途分四组：

### A. 环境初始化（从干净 OS 到可构建）

| 文件 | 角色 | 执行角色 | 幂等 | 典型用法 |
|---|---|---|---|---|
| **`bootstrap.sh`** | 一键环境初始化总入口。8 步：① 关 SELinux/防火墙 → ② dnf 装系统包（gcc/cmake/openssl/libpq/nginx 等）→ ③ 清旧 FastAPI systemd 单元 → ④ 编译安装 Drogon + Trantor（静态库到 `$IBMCP_BUILD_DIR`）→ ⑤ 调 `build_drivers.sh` 装协议驱动 → ⑥ 装 Docker CE + compose → ⑦ 起 PG/Redis/Node-RED 容器 → ⑧ 配 Nginx + 放开 ptrace | root（`sudo`） | 是（每步先检测再装） | `sudo bash bootstrap.sh` |
| **`build_drivers.sh`** | 协议驱动库编译安装。mosquitto 走 dnf；libmodbus / open62541 优先 dnf，失败则从 GitHub 源码编译到 `$IBMCP_BUILD_DIR/{libmodbus,open62541}`。最后**生成 `env.sh`**（聚合 PKG_CONFIG_PATH / LD_LIBRARY_PATH / PATH）并**注册 `/etc/ld.so.conf.d/ibmcp-build.conf` + ldconfig**，让 systemd 等非登录 shell 也能找到运行时 .so | root（`sudo`） | 是 | `sudo bash build_drivers.sh` |

### B. 构建与部署

| 文件 | 角色 | 执行角色 | 幂等 | 典型用法 |
|---|---|---|---|---|
| **`build.sh`** | 后端 C++ 编译。检查依赖（缺驱动库时自动调 `build_drivers.sh`）→ cmake 配置 → 编译 → 安装到 `dist/` → `ldd` 校验零 not found。产物：`dist/bin/ibmcp_server`（含采集器静态库 + Drogon 静态库） | 普通用户 | 否（每次重编） | `bash build.sh` |
| **`deploy.sh`** | 一键部署到运行态。支持两种模式：`--bundle <tarball>` 从离线包还原（跳过编译）；否则走 `build.sh` 编译。之后：起容器 → 跑 SQL 迁移（幂等）→ 前端静态 → 配 nginx → 装 systemd 服务 → 健康检查。参数：`--bundle/--full/--slim/--force-migrate/--user/--port/--project-root/--frontend-dist` | root（`sudo`） | 大部分 | `sudo bash deploy.sh` 或 `sudo bash deploy.sh --bundle /tmp/x.tar.gz` |
| **`make-bundle.sh`** | 离线编译产物打包（在**已成功构建**的机器上跑）。把 `dist/` + `$IBMCP_BUILD_DIR` +（可选 `SAVE_IMAGES=1`）docker 镜像打成 `ibmcp-bundle-*.tar.gz`，供 `deploy.sh --bundle` 在同架构机器还原 | 普通用户 | — | `bash make-bundle.sh` |

### C. 开发辅助

| 文件 | 角色 | 执行方式 | 说明 |
|---|---|---|---|
| **`dev-env.sh`** | 欧拉终端开发引导。`source` 后：加载 `env.sh` → cd 到项目根 → 定义别名（`ibmcp-build/run/deploy/migrate/up/down/logs/psql`）→ 提示符加 `[ibmcp]` 前缀。带防重复加载守卫（`_IBMCP_DEV_ENV_LOADED`） | `source dev-env.sh`（不能直接 bash 执行，否则别名/cd 不生效） | 也可写进 `~/.bashrc` 自动引导 |
| **`check_toolchain.sh`** | 工具链/依赖**只读体检**。检查 gcc/g++/cmake/gdb、Drogon、libpq/openssl/zlib、协议驱动库、`ptrace_scope`。不修改任何东西，用于排障或确认环境就绪 | `bash check_toolchain.sh` | 无 shebang，直接 bash 跑即可 |
| **`clear-root.sh`** | 清空欧拉项目根源码，**但保留运行产物**（`dist/`、`frontend-dist/`，以及 `.git/`）。用于"重置源码到干净态 → 再 eSync 重新同步"。支持 `--dry-run` 预览、`-y` 跳过确认、`--root` 改路径、`--keep` 追加保留项；逐个删除并捕获失败，删完列目录做门禁验证 | `bash clear-root.sh` | 清完后回 Windows 侧跑 `eSync` 恢复源码 |

### D. 基础设施

| 文件 | 角色 | 说明 |
|---|---|---|
| **`docker-compose.yml`** | 三个纯服务容器定义：PostgreSQL 15（:5432）、Redis 7（:6379）、Node-RED（:1880）。各带命名卷（`ibmcp_pg_data` / `ibmcp_redis_data` / `ibmcp_nodered_data`）和健康检查。**后端 + Nginx 不容器化**（需访问宿主 libpq 与协议驱动 .so），裸机跑 | `docker compose -f docker-compose.yml up -d` |
| **`euler-remote.ps1`** | **Windows 11 开发侧**的远程连接工具（PowerShell）。`dot-source` 后提供：`eInitKey`（装 SSH 公钥免密）、`e`（远程执行）、`esudo`（远程 sudo，自动喂密码）、`eup`/`edown`（scp 上传/下载）、`eSync`（tar over ssh 同步源码到欧拉，带排除清单）、`eResetRoot`（清远端旧 FastAPI 残留）。**含明文密码，已被 `.gitignore` 排除（`deployments/openeuler/euler*.ps1`），不入仓库** | PS：`. .\euler-remote.ps1` 后调函数 |

---

## 2. 脚本调用关系

```
                    ┌──────────────────┐
                    │  bootstrap.sh    │  干净 OS → 可构建环境
                    │  (sudo, 8 步)    │
                    └────────┬─────────┘
                             │ 第 5 步调用
                             ▼
                    ┌──────────────────┐
                    │ build_drivers.sh │  装驱动库 + 生成 env.sh + 注册 ldconfig
                    └────────┬─────────┘
                             │ 也被 build.sh 缺库时调用
                             ▼
   ┌───────────────────────────────────────────────┐
   │              build.sh  (编译)                  │
   │  产物 → dist/bin/ibmcp_server                  │
   └───────────────┬───────────────────┬───────────┘
                   │                   │
          直接部署 ▼                   ▼ 打包离线
        ┌────────────────┐    ┌──────────────────┐
        │  deploy.sh     │    │ make-bundle.sh   │
        │ (sudo, 一键部署) │    │  (生成 tarball)   │
        └────────┬───────┘    └────────┬─────────┘
                 │ --bundle 还原        │
                 └──────────────────────┘
                          目标机器
```

`docker-compose.yml` 被 `bootstrap.sh`（第 7 步）和 `deploy.sh`（第 2 步）调用；`dev-env.sh` / `check_toolchain.sh` / `clear-root.sh` / `euler-remote.ps1` 是独立辅助，不参与自动流程。

其中 `clear-root.sh` 与 `euler-remote.ps1` 的 `eSync` 配对构成一条**手动重置链路**：`clear-root.sh` 清空欧拉端源码（保留 `dist`/`frontend-dist` 运行产物）→ Windows 侧 `eSync` 重新同步最新源码。用于欧拉端源码脏了、或本地改动后想让远端回到完全干净的最新态。

---

## 3. 典型使用流程

### 流程 1：干净 openEuler 机器首次部署（在线）

```bash
# 1. 同步源码（Windows 侧）
. .\deployments\openeuler\euler-remote.ps1
eSync

# 2. 欧拉端：初始化环境 + 编译 + 部署
sudo bash deployments/openeuler/bootstrap.sh
bash deployments/openeuler/build.sh
sudo bash deployments/openeuler/deploy.sh
```

### 流程 2：日常开发迭代（Windows 改代码 → 欧拉跑）

```powershell
# Windows：同步源码
. .\deployments\openeuler\euler-remote.ps1
eSync

# 欧拉终端：已 source dev-env.sh 的话
ibmcp-build          # 等价于 bash build.sh
sudo bash deployments/openeuler/deploy.sh
# 或只手动跑：ibmcp-run
```

### 流程 3：离线交付到另一台同架构机器

```bash
# 源机器（已构建成功）
bash deployments/openeuler/make-bundle.sh          # 或 SAVE_IMAGES=1 bash make-bundle.sh
scp /tmp/ibmcp-bundle-*.tar.gz 目标机:/tmp/

# 目标机器（需先有基础环境：docker/nginx/驱动库，或先 bootstrap.sh）
sudo bash deployments/openeuler/deploy.sh --bundle /tmp/ibmcp-bundle-*.tar.gz
```

> 更完整的离线迁移方案见 `docs/0-Drogon/07-跨机器迁移与离线交付方案.md`。

### 流程 4：排障 / 确认环境

```bash
bash deployments/openeuler/check_toolchain.sh       # 看缺什么
ldd ~/ibmcp/dist/bin/ibmcp_server | grep 'not found'
sudo journalctl -u ibmcp-api -n 50 --no-pager
```

### 流程 5：清空欧拉端源码 → 重新同步最新（重置链路）

当欧拉端源码脏了、混入了不该有的文件，或本地大改后想让远端回到完全干净的最新态：

```bash
# 1. 欧拉端：清空源码（保留 dist/frontend-dist 运行产物，运行中服务不中断）
bash deployments/openeuler/clear-root.sh --dry-run   # 先预览将删/将保留
bash deployments/openeuler/clear-root.sh -y          # 确认后正式清空（-y 跳过交互）
```

```powershell
# 2. Windows 侧：重新同步最新源码
. .\deployments\openeuler\euler-remote.ps1
eSync
```

> `clear-root.sh` 只删源码、不碰 `dist`/`frontend-dist`，所以运行中的 `ibmcp-api` 服务不会中断；`eSync` 完成后源码即与本地一致。若源码有改动需重新编译，再跑 `bash build.sh`。

---

## 4. eSync 同步范围与排除清单

`eSync`（Windows 侧 `euler-remote.ps1` 提供）把本地项目根打包成 tar.gz → scp 上传 → 欧拉端解压。**同步范围 = 项目根下除排除清单外的全部源码**。排除清单如下（按类别）：

| 类别 | 排除项 | 原因 |
|---|---|---|
| 版本控制 / IDE / 本地缓存 | `.git` `.idea` `.vscode` `.trae` `.ide-deps` `.cache` `.clangd` | 纯本地，欧拉不需要 |
| 前端源码与依赖 | `frontend` `node_modules` | 前端在 Windows 构建，源码与依赖不传；产物走单独上传 |
| 构建产物 / 缓存 | `cmake-build-*` `backend/build` `build` `build2` `dist` `compile_commands.json` | 欧拉端 `build.sh` 自行生成 |
| 仅 Windows 运行的工具 | `euler-remote.ps1` | 只在 Windows 侧用，且含明文密码 |
| 文档 | `docs` | 可选，不传以瘦身同步包 |
| 单测 | `backend/collector/tests` | 不编进主二进制 |
| 可选辅助（当前流程不用） | `packaging/docker` `make-bundle.sh` | 离线打包/docker 构建，日常同步不需要 |
| 临时 / 日志 / marker | `_fix_temp` `*.log` `.clion.source.upload.marker` `.remote-include` `.screenshots` | 杂项 |

**两个"不传但必须单独处理"的关键项：**

- **`dist/`**：后端二进制，`eSync` 不传。欧拉端由 `build.sh` 生成；清空源码时务必用 `clear-root.sh`（它会保留 `dist`），不要手动 `rm -rf` 项目根，否则服务起不来。
- **`frontend-dist/`**：前端产物，`eSync` 不传。由 Windows 侧 `eup frontend\dist /home/meng/ibmcp/frontend-dist` 单独上传；`clear-root.sh` 同样会保留它。

> `clear-root.sh` 本身**不在**排除清单里，会随 `eSync` 同步到欧拉——这是正确的，因为清空操作要在欧拉端执行它。

---

## 5. 关键约定与注意事项

- **路径硬编码**：`backend/CMakeLists.txt` 第 13 行硬编码 `IBMCP_BUILD_DIR=/home/meng/ibmbuild`；各脚本默认值与此对齐。换用户/路径时需同步改脚本默认值或用环境变量覆盖。
- **`env.sh` 是自动生成的**：由 `build_drivers.sh` 扫描 `$IBMCP_BUILD_DIR` 下各子目录生成，**勿手改**。它只在交互式 shell 生效；systemd 服务靠 `/etc/ld.so.conf.d/ibmcp-build.conf` 注册运行时库（`build_drivers.sh` 已处理）。
- **`euler-remote.ps1` 编码**：含中文，必须存为 **UTF-8 with BOM**，否则 PowerShell 5 按 GBK 误读导致 `dot-source` 加载失败。
- **`euler-remote.ps1` 含密码**：已在 `.gitignore` 排除，不传欧拉（`eSync` 排除清单也排除了它自身）。
- **`make-bundle.sh` 不上传欧拉**：在 `eSync` 排除清单里，它在欧拉本地运行。
- **后端/Nginx 裸机跑**：不放进 docker，因为要直接访问宿主的 libpq 和协议驱动 `.so`；docker-compose 只管三个纯服务容器。

---

## 6. 常见问题与排障

| 现象 | 原因 | 处理 |
|---|---|---|
| `euler-remote.ps1` dot-source 报「字符串缺少终止符」、行号错位、函数不可用 | 含中文的 `.ps1` 存成了 UTF-8 **无 BOM**，PS5 按 GBK 误读，多字节序列吞掉换行/引号 | 重存为 **UTF-8 with BOM**（首字节 `239,187,191`）。命令：`[IO.File]::WriteAllText($f, [IO.File]::ReadAllText($f,[Text.Encoding]::UTF8), (New-Object System.Text.UTF8Encoding($true)))` |
| `eSync` 报 `gzip: stdin: not in gzip format` / `tar: Child died with signal 13` | 旧版 `eSync` 用 `tar \| ssh` 管道，PowerShell 文本管道损坏了 gzip 二进制流 | 已改为「本地打包 → scp → 远程解压」三步法。确保用的是最新版 `euler-remote.ps1` |
| `docker ps` / `docker images` 输出空，但端口（5432/6379/1880）确实在监听 | 当前用户（`meng`）不在 `docker` 组，无权列出容器 | 用 `sudo docker ps`；或 `sudo usermod -aG docker meng` 后**重新登录**生效 |
| 清空源码后 `ibmcp-api` 起不来、`ExecStart` 指向的二进制不存在 | 手动 `rm -rf` 了项目根，把 `dist/` 一起删了；`eSync` 不会传 `dist` | 用 `clear-root.sh` 清空（保留 `dist`）；若已误删，重新 `bash build.sh` 生成，或从备份恢复 |
| `eSync` 后欧拉端多了 `.cache` / `.clangd` 等本地 IDE 文件 | 旧版排除清单没含这些项 | 更新到最新版 `euler-remote.ps1`（排除清单已含 `.cache` `.clangd`），并手动 `rm -rf` 欧拉端残留 |
| PowerShell 远程命令里的 `>` / `2>` 重定向不生效、报错 | PS5 会拦截远程命令字符串中的重定向符 | 把脚本写到本地临时文件，base64 编码后：`ssh ... "printf '%s' '<b64>' \| base64 -d > /tmp/x.sh && bash /tmp/x.sh"` |
| 改了源码但欧拉端行为没变 | `eSync` 只传源码，运行的是 `dist/bin/ibmcp_server`，需重新编译 | `eSync` 后跑 `bash build.sh`，再 `sudo systemctl restart ibmcp-api` |
| IDE 满屏「语法错误」但欧拉端编译通过 | Windows 本地 IDE 解析不到欧拉的 drogon 头文件 → 级联误报（旧的本地 `.ide-deps` 镜像方案已废弃） | 本项目已用 **CLion 远程工具链（Windows 主开发 → 同步欧拉）**，索引/编译/调试用欧拉真实头文件，详见 [`docs/0-Drogon/08-CLion远程开发配置.md`](../../docs/0-Drogon/08-CLion远程开发配置.md)；判定正确性始终以欧拉端 `build.sh` 为准（0 error 即正确） |
