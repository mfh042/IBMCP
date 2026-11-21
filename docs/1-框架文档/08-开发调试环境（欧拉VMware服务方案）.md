# IBMCP 开发调试环境（Windows + openEuler 虚拟机）

> 版本：v9.0　　编写日期：2026-11-20
>
> **文档分工**：本文负责「为什么这样分工 + Windows 侧怎么开发调试 + 日常怎么跑」。
> **从零搭建虚拟机、装依赖、编译、部署**见 [09-openEuler环境准备实操指南](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/09-openEuler环境准备实操指南.md)，本文不重复。
> 两份文档冲突时，以仓库脚本（`deployments/`、`packaging/`、`backend/app/core/config.py`）为准。
>
> 配套契约：[01-架构设计](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/01-IBMCP-FastAPI重构架构设计.md) · [05-pybind11绑定契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md) · [07-前端契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/07-Vue3前端架构与多端交付契约.md)

## 0. 环境口径（先对齐）

| 项 | 值 | 说明 |
|----|----|------|
| Windows 项目根 | `E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP` | 代码唯一源头 |
| 欧拉 IP | `192.168.80.135` | 本文示例 IP，按实际替换 |
| 欧拉项目路径 | `/home/meng/ibmcp` | SSH/CLion 统一用户 `meng`；`bootstrap.sh` 用 root |
| Windows Python | **uv 托管 3.11.18**，项目根 `.venv` | 日常 Python 开发/调试 |
| 欧拉 Python | **系统自带 python3.11** + `venv` | 仅作 `_core.so` 的 ABI 宿主 + 生产运行时 |
| 数据服务 | 欧拉 Docker：PG15 / Redis7 / Node-RED | 端口 5432 / 6379 / 1880 |
| 前端 | Windows 构建 / Vite dev | 欧拉不装 Node |

> IP、网段、网关按自己环境调整；仓库脚本里的示例同样是 `192.168.80.135`。

## 1. 方案与职责

### 1.1 混合部署

| 部署方式 | 服务 | 原因 |
|----------|------|------|
| Docker Compose | PostgreSQL 15 · Redis 7 · Node-RED | 纯服务、无编译依赖，数据卷持久化 |
| 裸机 dnf | Nginx · gcc/g++/cmake · `_core.so` | pybind11 扩展必须用宿主 Python 编译；Nginx 托管前端 + 反代 |
| `pip install .` | FastAPI 及依赖（进欧拉 venv） | 与 `_core.so` 同源，供生产运行 |

### 1.2 开发期分工

| 能力 | Windows 11 | openEuler 虚拟机 |
|------|------------|------------------|
| Python 解释器 | ✅ 主力（uv 3.11.18） | ⚠️ 系统 python3.11，仅 ABI 宿主 |
| FastAPI 运行 | ✅ `COLLECTOR_BACKEND=stub` | ✅ 仅 `native` 联调/部署 |
| C++ 编译 / `_core.so` | ❌ | ✅ 唯一产地 |
| PG / Redis / Node-RED | ❌ 直连欧拉 | ✅ Docker |
| Nginx | ❌ | ✅ 裸机（部署期） |
| Vite dev / `npm run build` | ✅ 唯一构建地 | ❌ 只托管 `dist/` |
| gdbserver 调试 | ✅ 客户端 | ✅ 服务端 |

一句话：**Python 用 Windows 跑，C++ 用欧拉编和跑，数据服务在欧拉 Docker，最终整套交付到欧拉。**

### 1.3 为什么这样分

- **Python 能放 Windows**：`backend/` 是纯 Python；唯一平台相关的是 `industrial_collector._core`，由 `COLLECTOR_BACKEND` 隔离。`stub` 加载 `app/core/collector/stub.py`，不 import `.so`，可覆盖全部业务逻辑。
- **C++ 必须在欧拉编译**：① `.so` 是 ELF，Windows 无法加载；② 链接欧拉 glibc/libstdc++；③ `libmodbus` / `open62541` / `libmosquitto` 只有 Linux 版。
- **数据服务放欧拉 Docker**：与生产同款、贴近现场设备网段，且不污染 Windows。

> `COLLECTOR_BACKEND` 只接受 `stub` / `native`（不是 `ffi`）：Windows 必须 `stub`，欧拉用 `native`。

### 1.4 数据流

```
开发：Windows 改 Python → 本地 stub（8000）→ 读写欧拉 Docker PG/Redis
      Windows 改 C++   → CLion 上传 → 欧拉 build.sh → native 联调（192.168.80.135:8000）
      Windows 改前端   → Vite HMR（5173）→ proxy 到 127.0.0.1:8000 或 欧拉:8000

部署：Windows npm run build → dist\* 上传 frontend-dist
      欧拉 build.sh → alembic upgrade head → systemd 起 FastAPI → Nginx 托管/反代
```

## 2. 目录映射与纪律

```
Windows（源头 + Python 运行）                       欧拉 /home/meng/ibmcp（编译 + 部署）
  backend\        ← 本地 stub 跑                    ← 仅部署/联调时需要
  frontend\       ← Vite dev / npm run build        ← 仅上传 dist 产物
  collector-cpp\  ← CLion 编辑                      ← 自动同步，编译在欧拉
  collector-python\ ← 扩展 Python 包壳              ← 自动同步
  deployments\    ← 脚本                            ← 自动同步
  pyproject.toml                                    ← 自动同步
  .venv\          ← Windows 虚拟环境                ← 绝不上传
```

| 纪律 | 说明 |
|------|------|
| D1 | 代码只在 Windows 编辑，Windows 是唯一源头 |
| D2 | C++ 保存即自动上传（CLion SFTP） |
| D3 | 开发期不上传 `backend/`（Python 在 Windows 跑） |
| D4 | 不在欧拉手动改代码，避免被同步覆盖 |
| D5 | git 只在 Windows 执行 |
| D6 | `.venv/`、`venv/`、`node_modules/`、`build/` 绝不同步 |
| D7 | Deployment 设为 Never download，防止 CMakeCache 反向污染 |
| D8 | Windows=`stub`，欧拉=`native` |

## 3. Windows Python 环境（PyCharm）

### 3.1 一键初始化

```powershell
cd E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP
powershell -ExecutionPolicy Bypass -File deployments\dev-windows\bootstrap.ps1
# 手工等价：uv python install 3.11.18 ; uv sync
uv run python --version     # Python 3.11.18
```

`uv sync` 按 `uv.lock` 在项目根创建 `.venv`，含 dev 组（pytest/ruff/mypy），不装 `_core.so`。

### 3.2 PyCharm 解释器

```
Ctrl+Alt+S → Python | Interpreter → Add Interpreter → Add Local Interpreter
  Virtualenv Environment → Existing
  E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\.venv\Scripts\python.exe
```

必须用**本地解释器**，不要配 SSH 远程解释器。

### 3.3 `backend/.env`

```powershell
cd E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\backend
Copy-Item .env.example .env
```

只改三处 host，并确认 stub：

```ini
PG_HOST=192.168.80.135
PG_PORT=5432
REDIS_URL=redis://192.168.80.135:6379/0
COLLECTOR_BACKEND=stub
```

> 必须在 `backend/` 目录下启动，因为 `config.py` 用相对路径 `env_file=".env"`。

### 3.4 启动

```powershell
cd E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\backend
uv run uvicorn app.main:app --reload --host 127.0.0.1 --port 8000
# http://127.0.0.1:8000/api/v1/docs
```

### 3.5 数据库工具

| 服务 | Host | Port | DB / 备注 |
|------|------|------|-----------|
| PostgreSQL | `192.168.80.135` | 5432 | `ibmcp` / `ibmcp` / `ibmcp` |
| Redis | `192.168.80.135` | 6379 | 无密码 |

PyCharm Database 或 DBeaver 直连即可，不需要 SSH 隧道。

## 4. CLion：C++ 远程编译

### 4.1 Remote Host Toolchain

```
Ctrl+Alt+S → Build, Execution, Deployment | Toolchains → + → Remote Host
  Credentials: 选 SSH 配置 euler-ibmcp
  CMake: /usr/bin/cmake   C Compiler: /usr/bin/gcc   C++: /usr/bin/g++
  Debugger: /usr/bin/gdb
  → Test Connection
```

CMake 配置、编译、链接全部在欧拉执行，Windows 只编辑和索引。

### 4.2 Deployment + CMake Profile

```
Ctrl+Alt+S → Build, Execution, Deployment | Deployment → + → SFTP
  Name: euler-ibmcp
  SSH configuration: euler-ibmcp
  Root path: /home/meng/ibmcp
  Mappings: Local E:\...\02-IBMCP  →  Deployment path /
  → Use as Default

Options:
  Upload changed files automatically to the default server: Always
  Delete remote files when local are deleted: ✅
  Skip external changes: ✅
```

CMake Profile 用上面的 Remote Host，Generation path 指向 `/home/meng/ibmcp/collector-cpp/build`。日常编译推荐直接在欧拉跑 `build.sh`（见 09 号 §6），CLion Profile 主要用于索引和远程调试。

### 4.3 首次上传与验证

```
右键项目根 → Deployment | Upload to → euler-ibmcp
```

```bash
ls /home/meng/ibmcp/collector-cpp/CMakeLists.txt
ls /home/meng/ibmcp/collector-python/
ls /home/meng/ibmcp/pyproject.toml
ls /home/meng/ibmcp/.venv        # 应不存在
ls /home/meng/ibmcp/frontend     # 应不存在
```

自动上传自检：在 `.cpp` 加一行注释 → `Ctrl+S` → File Transfer 出现记录 → 欧拉侧 `grep` 到内容。

### 4.4 排除项

```
Deployment | Options → Excluded Paths →
  .venv
  venv
  frontend
  collector-cpp\build
  __pycache__
  .ruff_cache
  ibmcp.egg-info
  .git
```

上传报 `Permission denied`：`sudo chown -R meng:meng /home/meng/ibmcp`。

## 5. 调试

### 5.1 Python：PyCharm 本地调试

```
Run → Edit Configurations → + → Python
  Module name: uvicorn
  Parameters: app.main:app --host 127.0.0.1 --port 8000 --reload
  Working directory: E:\...\02-IBMCP\backend
  Interpreter: 项目根 .venv（3.11.18）
```

断点、变量、单步全在 Windows 本地进程，零网络延迟；数据库仍是欧拉的真实 PG/Redis。
启动命令固定 `app.main:app`（不是 `create_app --factory`）；`COLLECTOR_BACKEND` 只能是 `stub`/`native`。

### 5.2 C++：gdbserver 远程附着

```bash
# 欧拉
cd /home/meng/ibmcp && source venv/bin/activate
cd backend
COLLECTOR_BACKEND=native uvicorn app.main:app --host 0.0.0.0 --port 8000 &
gdbserver :1234 --attach $!
```

CLion：`Run → Attach to Process → Remote Host → euler-ibmcp → gdbserver :1234`；
未自动探测时手工填 `192.168.80.135:1234`。

ptrace 需为 0（bootstrap.sh 已处理）：

```bash
cat /proc/sys/kernel/yama/ptrace_scope     # 0
```

### 5.3 Debug 编译（断点需要符号）

`build.sh` 默认 Release，无 `-g`，断点会报 "No executable code found"。需要单步时：

```bash
cd /home/meng/ibmcp && source venv/bin/activate
cmake -S collector-cpp -B collector-cpp/build \
  -DCMAKE_BUILD_TYPE=Debug \
  -Dpybind11_DIR="$(python -c 'import pybind11; print(pybind11.get_cmake_dir())')" \
  -DPython_EXECUTABLE="$VIRTUAL_ENV/bin/python"
cmake --build collector-cpp/build -j"$(nproc)"
cmake --install collector-cpp/build --prefix collector-python
pip install . --force-reinstall --no-deps
```

### 5.4 跨语言断点

只在欧拉做（Windows stub 没有 C++）：

1. CLion Debug 编译并上传；
2. 欧拉起 `native` 实例 + `gdbserver :1234`；
3. CLion Attach，在 `collector-cpp/src/` 设断点；
4. 另一终端 `curl` 触发采集 → 断点命中；
5. 先停 gdbserver，再 kill uvicorn。

> GIL/死锁规则以 [05-绑定契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md) 为准。跨语言排查也可在 `_core` 入口打 `std::cerr` / `py::print` 日志。

## 6. 前端开发

```powershell
cd E:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP\frontend
npm install
npm run dev          # http://localhost:5173
```

`vite.config.ts` 默认代理 `http://127.0.0.1:8000`，并支持环境变量切换：

```powershell
VITE_API_TARGET=http://192.168.80.135:8000 npm run dev   # native 联调
```

| 场景 | target |
|------|--------|
| 日常 stub | `http://127.0.0.1:8000` |
| native 联调 | `http://192.168.80.135:8000` |

`/api` 与 `/ws` 都由 Vite 代理（`ws: true`），切换后需重启 Vite；8000 端口同一时间只能有一个主人。

## 7. 端口与连通性

| 端口 | 服务 | 跑在哪 | 使用者 |
|------|------|--------|--------|
| 22 | SSH | 欧拉 | CLion / scp / 运维 |
| 5432 | PostgreSQL | 欧拉 Docker | Windows Python / DBeaver |
| 6379 | Redis | 欧拉 Docker | Windows Python |
| 1880 | Node-RED | 欧拉 Docker | 浏览器 |
| 8000 | FastAPI | Windows（stub）或欧拉（native） | 浏览器 / Vite / Nginx |
| 80 | Nginx | 欧拉裸机 | 浏览器（部署后） |
| 1234 | gdbserver | 欧拉 | CLion（仅调试） |
| 5173 | Vite dev | Windows | 浏览器 |

```powershell
foreach ($p in 22,5432,6379,1880) {
  $r = Test-NetConnection 192.168.80.135 -Port $p -WarningAction SilentlyContinue
  Write-Host "${p}: $($r.TcpTestSucceeded)"
}
# http://127.0.0.1:8000/api/v1/docs   → Windows stub
# http://192.168.80.135:8000/api/v1/docs → 欧拉 native
# http://192.168.80.135:1880          → Node-RED
```

桥接网络下直连即可，**不需要 SSH 隧道**；仅在需要统一 `127.0.0.1` 口径或远程接入时再开。

## 8. 日常工作流

### 8.1 每日开工清单

```
□ 启动欧拉虚拟机，确认 IP 192.168.80.135
□ ssh meng@192.168.80.135 "docker ps"   → 三个容器 Up
   未起：docker compose -f deployments/openeuler/docker-compose.yml up -d
□ 项目根 uv run python --version → 3.11.18
□ PyCharm 解释器为 .venv（不是 SSH）
□ backend\.env：stub + PG_HOST=192.168.80.135
□ PyCharm Debug 起 FastAPI（8000）
□ frontend npm run dev（5173）
□ CLion 打开 collector-cpp，Toolchain 为 Remote Host
□ 连通性：22 / 5432 / 6379 / 1880 全 True
```

### 8.2 Python 循环

编辑 → 保存（`--reload` 自动重启）→ 本地断点调试 → 读写欧拉真实库。全程无上传流量。

### 8.3 C++ 循环

编辑 → `Ctrl+S` 自动上传 → 欧拉 `bash deployments/openeuler/build.sh` → 冒烟 / gdbserver 调试。

### 8.4 联调顺序

```
1. Windows + stub：业务链、配置向导、历史/控制/WS（无硬件）
2. 欧拉 + native：编译成功、import 正常、空 plan 下发返回 0
3. 模拟器：Node-RED / Modbus TCP / MQTT，验证协议与 scale/offset
4. 真实设备：逐台 PLC 接入，逐协议验证 + 压力测试
5. 全链路：配置 → 下发 → 在线 → 实时 → 历史 → 报警
```

阶段 3 不可省，用来区分「代码问题」与「PLC 配置问题」。

### 8.5 重新编译 C++

```bash
cd /home/meng/ibmcp && source venv/bin/activate
bash deployments/openeuler/build.sh                                  # 增量
rm -rf collector-cpp/build && bash deployments/openeuler/build.sh    # 全量
python -c "import industrial_collector._core as c; print(c.Collector.instance().health())"
```

## 9. 常见问题（开发侧）

| 症状 | 排查 / 修复 |
|------|-------------|
| Windows 连不上欧拉 | `ping` 不通 → 桥接网卡；通但 SSH 不行 → `~/.ssh/config` / 免密 |
| CLion 保存后欧拉不更新 | Deployment 自动上传是否为 Always；排除项是否误排 `collector-cpp` |
| CLion 右键无 Deployment | 需 Professional 版 + SFTP 插件；先建一个 SFTP 配置 |
| 连不上 5432/6379 | 欧拉 `ss -tlnp \| grep 5432`；`docker ps`；防火墙 |
| Windows FastAPI 起不来 | 是否在 `backend/` 下启动；`COLLECTOR_BACKEND` 是否误设 `native` |
| `ModuleNotFoundError: industrial_collector._core`（Windows） | 正常：Windows 无 Linux `.so`，改 `stub` |
| `RuntimeError: ... _core 未安装` | `native` 但未编译：改 stub 或跑 `build.sh` |
| `dynamic module does not define module export function` | ABI 不匹配：确认 venv 是 python3.11，未误用 uv 建解释器 |
| `ValueError: unknown collector backend: ffi` | 只能是 `stub` / `native` |
| 断点 "No executable code found" | 用 `-DCMAKE_BUILD_TYPE=Debug` 重编 |
| `ptrace: Operation not permitted` | `echo 0 \| sudo tee /proc/sys/kernel/yama/ptrace_scope` |
| `libmodbus.so: cannot open shared object file` | 源码装库后 `ldconfig` |
| 前端 `/api` 404 | Vite target 指向是否与 FastAPI 实际运行位置一致；切换后重启 Vite |
| 前端 WS 连不上 | `/ws` 代理需 `ws: true`，target 同源 |
| 前端 5173 被占用 | `strictPort: true`，换端口或杀进程 |

> 环境搭建/部署类问题（VM、Docker、build.sh、Nginx、systemd）见 09 号 §10。

## 10. 资源占用参考

| 项目 | 占用 |
|------|------|
| 欧拉开发态（PG+Redis+Node-RED+venv，不含编译） | 内存 ~1 GB，磁盘 ~2 GB |
| C++ 编译临时 `collector-cpp/build` | ~2 GB（Debug 更大，可随时删） |
| Docker 镜像 | ~1 GB |
| Windows `.venv` + `node_modules` + IDE 索引 | ~1.8 GB |

采集压力大时欧拉内存可上浮到 ~3.5 GB；虚拟机建议 ≥ 8 GB 内存 / 120 GB 磁盘。

## 11. 版本记录

| 版本 | 日期 | 变更 |
|------|------|------|
| v9.0 | 2026-11-20 | 与 09 号去重拆分：本文只保留方案、Windows 开发、调试、工作流、开发侧排查；搭建/部署步骤移入 09；统一 IP `192.168.80.135` 与 `collector-python/` 路径口径 |
| v8.0 | 2026-11-20 | Windows 改用 uv 3.11.18；欧拉用系统 python3.11 + venv；`bootstrap.sh` 收紧为 nginx+docker+C++ 工具链 |
| v7.1 | 2026-11-20 | 前端改为 Windows 构建、上传 `dist/` 静态产物 |
| v7.0 | 2026-11-20 | Python 改本地解释器，`stub`/`native` 分工固化，Deployment 范围收窄 |
