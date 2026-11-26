# Node-RED 前端集成（运行配置菜单启动 + iframe 嵌入）实施计划

## 目标
在前端「运行配置」菜单下新增「Node-RED 流编辑器」入口：页面顶部提供容器状态 + 启动/停止/重启控制条（admin 可操作），下方通过 **nginx 同源反代**把 Node-RED 编辑器以 iframe 嵌入前端显示界面。

## 一、仓库研究结论（基于实测）

1. **前端菜单/路由**：菜单硬编码在 [MainLayout.vue](../frontend/src/layouts/MainLayout.vue)（`运行配置` submenu 在 L52–61，`index="config"`）；路由在 [router/index.ts](../frontend/src/router/index.ts)，子路由挂在 `/` 的 MainLayout children 下，用 `meta.roles` 控制可见性。API 模块统一走 `@/comm/http` 的 `httpService`，路径形如 `/api/v1/...`（baseURL 为空，同源经 nginx `/api/` 反代）。
2. **后端控制器模式**：用 `ADD_METHOD_TO` 宏在 `.h` 里注册路由（drogon 自动扫描，无需改 main.cpp），`.cpp` 里 `ctrl::requireAnyRole(req, {"admin"})` 鉴权；返回统一信封 `{code,data,message}`。CMake `GLOB_RECURSE "src/*.cpp"` 自动收录新文件。
3. **后端能控 docker**：systemd 服务 `ibmcp-api` 以用户 `meng` 运行；`meng` 在 `wheel` 组且 `sudo -n docker` 免密（实测 `sudo -n docker ps` 成功）。故后端可 `sudo -n docker start/stop/restart/inspect ibmcp-nodered`。后端目前**无** shell 执行工具，需新增一个 `popen` 封装。
4. **Node-RED 可被 iframe**：`curl -I http://127.0.0.1:1880/` 无 `X-Frame-Options` / `Content-Security-Policy` 头，不阻挡 iframe。容器 `restart: unless-stopped`，手动停后不会自启 → 启停按钮有意义。
5. **nginx**：active server 块在 `/etc/nginx/conf.d/ibmcp.conf`（root 前端 dist，`/api/`、`/ws/push` 反代到 8000，`/` try_files SPA）。需新增 `location /nodered/`。
6. **欧拉构建/部署**：仓库 checkout 在 `/home/meng/ibmcp`；后端 build 目录 `cmake-build-debug-oe-remote`（Makefile）；运行二进制 `/home/meng/ibmcp/dist/bin/ibmcp_server`，systemd 服务 `ibmcp-api`。前端在本地构建后 scp `dist` 覆盖 `/home/meng/ibmcp/frontend-dist`。

## 二、关键设计决策（已与用户确认）

- **嵌入方式 = nginx 同源反代**：Node-RED 设 `httpAdminRoot: '/nodered'`，nginx `location /nodered/` → `127.0.0.1:1880`（透传前缀，含 WS Upgrade），iframe `src="/nodered/"`。避免跨域、端口暴露、绝对路径 404。
- **控制范围 = 完整控制条（admin）**：状态轮询 + 启动/停止/重启；engineer 可见状态与编辑器但无操作按钮（按钮按角色隐藏）。

## 三、改动文件清单

### 后端（欧拉编译）
- `backend/include/controller/node_red_controller.h`（新建）：注册 4 个路由。
- `backend/src/controller/node_red_controller.cpp`（新建）：实现 status/start/stop/restart，调 shell 封装。
- `backend/include/util/shell_util.h` + `backend/src/util/shell_util.cpp`（新建，或并入 controller）：`popen` 封装，返回 `{rc, output}`；后端无现有同类工具。
- 欧拉侧：scp 上述文件到 `/home/meng/ibmcp/backend/` → 编译 → 更新 `dist/bin/ibmcp_server` → 重启服务。

### Node-RED（容器内持久化卷）
- `/data/settings.js`：设置 `httpAdminRoot: '/nodered'`（默认注释/缺失），重启容器生效。同步更新仓库 [deployments/nodered/settings.js.patch](../deployments/nodered/settings.js.patch) 记录该定制。

### nginx（欧拉）
- `/etc/nginx/conf.d/ibmcp.conf`：新增 `location /nodered/ { proxy_pass http://127.0.0.1:1880; proxy_http_version 1.1; Upgrade/Connection/Host 头; proxy_read_timeout 3600s; }`，`nginx -t && systemctl reload nginx`。
- 仓库内同步一份到 [deployments/openeuler/](../deployments/openeuler/)（若该目录有 nginx 配置模板则一并更新；没有则在文档记录）。

### 前端（本地构建 + 部署）
- `frontend/src/api/nodeRed.ts`（新建）：`getStatus/start/stop/restart`。
- `frontend/src/views/node-red/NodeRedEditor.vue`（新建）：控制条 + iframe；容器未运行时显示占位+启动按钮，运行时加载 iframe（`:key` 触发生效后重载）；每 5s 轮询状态。
- `frontend/src/router/index.ts`：加路由 `node-red/editor`，`meta.title='Node-RED 流编辑器'`，`roles=['admin','engineer']`。
- `frontend/src/layouts/MainLayout.vue`：「运行配置」submenu 加 `<el-menu-item index="/node-red/editor">Node-RED 流编辑器</el-menu-item>`。
- `frontend/vite.config.ts`：dev 代理加 `/nodered` → `VITE_NODERED_TARGET || 'http://127.0.0.1:1880'`（生产走 nginx，不用此代理）。
- 确认使用的 Element Plus 图标（启动/停止/重启）在 `@element-plus/icons-vue` 真实导出（经验教训：先核验导出清单，不猜）。

### 文档
- [docs/0-Drogon/11-Node-RED集成方案.md](../docs/0-Drogon/11-Node-RED集成方案.md)：补「前端集成」小节（菜单、iframe 同源反代、启停 API）。
- [deployments/nodered/README.md](../deployments/nodered/README.md)：补 `httpAdminRoot` 与 nginx `/nodered/` 说明。

## 四、实施步骤（依赖顺序）

1. **Node-RED 侧**：欧拉容器内改 `/data/settings.js` 加 `httpAdminRoot: '/nodered'`，`docker restart ibmcp-nodered`。验证：`curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:1880/nodered/` → 200（无权限时 401 也算可达）。同步更新仓库 settings.js.patch。
2. **nginx 侧**：加 `location /nodered/`（含 WS Upgrade），`nginx -t` 通过后 reload。验证：`curl http://192.168.80.136/nodered/` 经 nginx 返回编辑器 HTML（不是前端 index.html）。
3. **后端侧**：写 `node_red_controller.h/.cpp` + shell 封装；scp 到欧拉 `/home/meng/ibmcp/backend/`；先确认 systemd `ExecStart` 路径与 build→dist 的更新方式；`cmake` + `make -j` 编译；替换二进制；`sudo systemctl restart ibmcp-api`。验证（用正式 admin 登录取 token）：
   - `GET /api/v1/node-red/status` → `data.container=running`, `data.editorReachable=true`。
   - `POST /api/v1/node-red/stop` → 200，状态变 stopped；`POST /api/v1/node-red/start` → 恢复 running。
4. **前端侧**：新增 api/view/路由/菜单项，加 vite 代理；`npm run build`；scp dist 覆盖欧拉 `frontend-dist`（先备份）。验证：浏览器硬刷新，「运行配置」出现新菜单项 → 进入页面 → 控制条状态正确 → 点「启动/停止/重启」生效 → iframe 显示 Node-RED 编辑器（含其自身登录）。
5. **文档**：更新 doc 11 与 nodered README。

## 五、依赖与注意事项

- `httpAdminRoot='/nodered'` 后，Node-RED 编辑器及 `/nodered/comms` WS、静态资源都在 `/nodered` 前缀下，nginx 必须**透传前缀**（`proxy_pass` 不带 URI）。
- iframe 内 Node-RED 仍走自身 `adminAuth`（admin/ibmcp2026），与 IBMCP 登录相互独立；这是预期行为（不做 SSO 穿透，避免越权）。
- 后端 `popen` 执行 docker 命令会阻塞当前 drogon IO 线程 1–数秒；启停为 admin 低频操作，可接受；用 `docker stop -t 5` 限制停止超时。若要更优雅可后续移到线程池（本次不做）。
- 后端只允许操作固定容器名 `ibmcp-nodered`，命令字符串常量写死，不接收用户输入拼命令，避免命令注入。
- 图标导入必须先核验 `@element-plus/icons-vue` 导出清单（经验教训 1004477）。
- 欧拉 SSH 操作注意 CRLF（脚本先 `sed -i 's/\r$//'`）、`meng` 不在 docker 组用 `sudo docker`、`docker exec -i` 传 stdin。

## 六、验证（完成后）
- 后端 API 四端点返回正确信封，角色鉴权生效（engineer 调 stop → 403）。
- `http://192.168.80.136/nodered/` 经 nginx 正常打开编辑器。
- 前端页面：未运行时显示「未运行 + 启动」；启动后 iframe 加载编辑器；停止后 iframe 卸载、状态更新；全程 5s 轮询。
- Node-RED 重启后，原有 Modbus 模拟器流（1502）仍正常写寄存器（回归不破坏已打通链路）。

## 七、风险与回退
- **settings.js 改错致 Node-RED 起不来**：改前备份 `/data/settings.js`；启动失败则还原备份并 `docker restart`。
- **nginx 配置语法错致全站 502**：`nginx -t` 必过才 reload；失败回滚 ibmcp.conf。
- **后端编译失败/新二进制起不来**：编译前备份 `dist/bin/ibmcp_server`，失败则还原旧二进制 + 重启。
- **前端构建失败**：已备份 `frontend-dist`，失败还原。
