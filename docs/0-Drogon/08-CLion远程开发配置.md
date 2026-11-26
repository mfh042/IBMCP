# 08 - CLion 远程开发配置（Windows 主开发 → 同步欧拉）

> 适用场景：Windows 11 为**主要开发端**，在本地编辑源码，通过 CLion 自动同步到 openEuler 虚拟机进行**编译、运行、调试**。
> 本项目使用的是 **CLion 远程工具链（Remote Toolchain）+ Deployment 自动上传**模式，**不是** Gateway 全远程模式。源码以 Windows 本地为准。

---

## 1. 当前模式确认（已就绪）

本项目的 CLion 远程开发配置**已经存在并可用**，无需重新搭建。核心配置：

| 配置项 | 值 | 位置 |
|---|---|---|
| 部署服务器（自动上传） | `OE-Remote`（SFTP） | [.idea/deployment.xml](../../.idea/deployment.xml) |
| 上传映射 | 本地 `$PROJECT_DIR$` → 欧拉 `/home/meng/ibmcp` | deployment.xml 第 9 行 |
| CMake Profile | `Debug-OE-Remote`（工具链 `OE-Remote`） | [.idea/workspace.xml](../../.idea/workspace.xml) 第 29 行 |
| Run Configuration | `ibmcp_server`（CMake Application） | workspace.xml 第 367 行 |
| 本地构建目录 | `cmake-build-debug-oe-remote/` | CLion 自动生成 |

**工作流**：在 Windows 的 CLion 里编辑代码 → CLion 自动（或手动）上传到欧拉 → 用欧拉的 cmake/gcc 编译 → 在欧拉运行/调试。Windows 本地保留完整源码副本，是开发的主端。

> 已删除的旧本地 IDE 辅助文件（进了 Windows 回收站，可恢复）：`backend/.ide-deps/`、项目根 `compile_commands.json`、`.clangd`。`.vscode/` 仍保留（含 `sftp.json`），是给 VS Code 用的，**CLion 远程工具链不依赖它**——CLion 会自动从欧拉同步头文件到本地索引缓存，所以 `#include <drogon/...>` 不会报红。

---

## 2. 前置条件

### 2.1 欧拉端（`192.168.80.136`，用户 `meng`）— 已满足

| 项 | 要求 | 实际 |
|---|---|---|
| cmake | ≥ 3.20 | 3.27.9 ✅ |
| gcc / g++ | 支持 C++20 | 12.3.1 ✅ |
| gdb | 远程调试用 | 14.1 ✅ |
| make | — | 已装 ✅ |
| 项目源码 | 在 `/home/meng/ibmcp` | 已存在 ✅ |
| SSH | 密钥免密登录 | `id_ed25519` 已配 ✅ |

> ⚠️ `rsync` 当前**未安装**。CLion 远程工具链的部分同步操作可能用到 rsync（当前用 SFTP 上传不影响编译，但建议装上以避免个别操作报错）：
> ```bash
> ssh meng@192.168.80.136 "sudo dnf install -y rsync"
> ```

### 2.2 Windows 端

- **CLion**（已打开本项目，配置已在 `.idea/` 中）。
- SSH 私钥：`C:\Users\Administrator\.ssh\id_ed25519`（CLion 远程工具链的 SSH 配置里已引用）。

---

## 3. 日常开发工作流

1. **Windows 打开 CLion**，加载本项目（`e:\999-Dev\01-IBMCP-黑盒管控系统\02-IBMCP-drogon`）。
2. CLion 已配置好远程工具链 `OE-Remote`，底部状态栏可见。直接在本地编辑 `backend/src/**` 下的源码。
3. **同步到欧拉**：CLion 的 Deployment（`OE-Remote`）配置了自动上传——保存文件时会自动 SFTP 上传到 `/home/meng/ibmcp`。若未自动上传，可右键文件 → `Deployment → Upload to OE-Remote`，或菜单 `Tools → Deployment → Upload`。
4. **编译**：`Build → Build Project`（`Ctrl+F9`），CLion 在欧拉端用远程 cmake/gcc 编译，产物在 `cmake-build-debug-oe-remote/`（本地有缓存镜像，真正的编译在欧拉）。
5. **运行 / 调试**：见下文第 5、6 节。

> 提示：也可以用 `eSync`（`euler-remote.ps1`）批量同步整个项目到欧拉，适合一次性推送大量改动。日常单文件改动用 CLion 自动上传更即时。**不要同时用 CLion 自动上传和 eSync 改同一批文件**，以免互相覆盖。

---

## 4. CMake 配置（开箱即用）

`backend/CMakeLists.txt` 已硬编码依赖路径，CLion 远程端无需额外参数：

- 第 15 行：`set(IBMCP_BUILD_DIR "/home/meng/ibmbuild")`
- 第 17–24 行：自动设置 `CMAKE_PREFIX_PATH`（17–20，`ibmbuild/drogon/cmake`、`ibmbuild/trantor/cmake`）和 `PKG_CONFIG_PATH`（22–24，modbus / open62541 / mosquitto）。
- 第 26 行：`find_package(Drogon CONFIG REQUIRED PATHS ".../drogon/cmake" NO_DEFAULT_PATH)` 直接定位 Drogon 静态库。

CLion 的 `Debug-OE-Remote` Profile 已绑定远程工具链，Reload CMake 后底部 CMake 窗口应显示绿色成功，`#include <drogon/drogon.h>` 不报红。

---

## 5. 运行

有两种运行方式：

### 方式 A：CLion 内直接运行（开发调试用）

选中 `ibmcp_server` 运行配置（已默认选中），点 **Run**（`Shift+F10`）。CLion 会在欧拉端启动编译产物。

> ⚠️ 注意：如果 systemd 的 `ibmcp-api` 服务已经占着 8000 端口，CLion 直接 Run 会因端口冲突启动失败。开发调试前先停掉 systemd 服务：
> ```bash
> ssh meng@192.168.80.136 "sudo systemctl stop ibmcp-api"
> ```
> 调试结束后再用 `sudo systemctl start ibmcp-api` 恢复后台服务。

### 方式 B：systemd 后台服务（部署验证用）

```bash
ssh meng@192.168.80.136 "cd /home/meng/ibmcp && bash deployments/openeuler/build.sh && sudo systemctl restart ibmcp-api"
```

- `build.sh` 含完整依赖校验 + `ldd` 运行时库检查，产物安装到 `/home/meng/ibmcp/dist/`。
- 服务监听端口由 `dist/config.json` 的 `listeners[].port` 决定（当前为 **8000**，不是 8080）。
- 验证：`curl http://192.168.80.136:8000/api/v1/ping` 应返回 `{"code":0,...}`。

---

## 6. 调试

CLion 远程工具链的调试是**全远程**的，不需要手动起 gdbserver：

1. 先停掉 systemd 服务释放端口（`sudo systemctl stop ibmcp-api`），否则断点进程起不来。
2. 在代码行号左侧点击设置断点。
3. 选中 `ibmcp_server` 配置，点 **Debug**（`Shift+F9`）。
4. CLion 通过 SSH 在欧拉端启动 `gdb 14.1` 加载程序，命中断点后可查看变量、调用栈、单步、条件断点。

> 若要调试**已在运行的 systemd 进程**，用 CLion 的 `Run → Attach to Process`，选择远程的 `ibmcp_server` 进程（pid 可在欧拉端 `ps -ef | grep ibmcp_server` 查到）。

---

## 7. 常见问题

### 7.1 CLion 里点 Run/Debug 报端口被占用

systemd 的 `ibmcp-api` 服务正在后台运行并占用 8000 端口。先停掉：
```bash
ssh meng@192.168.80.136 "sudo systemctl stop ibmcp-api"
```

### 7.2 改了代码但欧拉端行为没变

两种可能：
- CLion 自动上传没触发：手动 `Tools → Deployment → Upload to OE-Remote`，或确认 `Settings → Build, Execution, Deployment → Deployment → Options` 里 `Upload changed files automatically to the default server` 设为 `On explicit save action` 或 `Always`。
- 运行的是旧二进制：CLion 重新 Build，或欧拉端 `bash build.sh && sudo systemctl restart ibmcp-api`。

### 7.3 索引报"找不到 `drogon/drogon.h`"

1. 确认底部 **CMake** 窗口 Reload 成功（无红色错误）。若 `find_package(Drogon)` 失败，检查 `/home/meng/ibmbuild/drogon/cmake/Drogon/DrogonConfig.cmake` 是否存在。
2. `File → Reload CMake Project`。
3. 仍不行：`File → Invalidate Caches → Invalidate and Restart`。

### 7.4 rsync 缺失的影响

当前用 SFTP 上传不影响编译运行。但若 CLion 同步时报 rsync 相关错误，装上即可：
```bash
ssh meng@192.168.80.136 "sudo dnf install -y rsync"
```

### 7.5 怎么确认构建/运行没被本地文件污染

- 欧拉端真实构建只看 `backend/CMakeLists.txt` → `/home/meng/ibmbuild`，与 Windows 本地任何文件无关；
- `eSync` 排除清单 + `.gitignore` 已排除 `.ide-deps`、`.clangd`、`compile_commands.json`、`.vscode`、`cmake-build-*`，这些永不入构建；
- 判定构建正确性以欧拉端 `bash deployments/openeuler/build.sh` 结果为准（0 error 即正确）。
