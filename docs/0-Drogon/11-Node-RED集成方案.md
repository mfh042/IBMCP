# 11 · Node-RED 集成方案

> 本文定义 Node-RED 在 IBMCP 项目中的**定位、边界、使用场景与落地路线**。所有现状数据来自对欧拉容器的实测（Node-RED v5.0.10 / Node v24.22.3）。深入各场景的实现细节会在落地时补充到对应小节。

---

## 0. 文档定位

回答三个问题：
1. Node-RED 现在是什么状态（§1）；
2. 它在本项目**该干什么、不该干什么**（§2）；
3. 具体怎么用、怎么落地（§3–§7）。

---

## 1. 现状盘点（2026-11-26 欧拉实测）

| 项 | 状态 | 说明 |
|----|------|------|
| 版本 | Node-RED **v5.0.10**，Node.js **v24.22.3** | `nodered/node-red:latest` 镜像 |
| 流文件 `flows.json` | 仅默认空 Flow 1 | **无任何业务流** |
| 第三方节点 | **零** | `/data/node_modules` 为空，modbus/opcua/postgresql/dashboard 全未装 |
| 鉴权 | **未启用** | `adminAuth` 注释掉，`http://<IP>:1880` 裸奔可直接进编辑器 |
| 凭证密钥 | **未设置** | `credentialSecret` 注释，日志有警告；容器重建/迁卷可能丢加密凭证 |
| 数据持久化 | ✅ 命名卷 `ibmcp_nodered_data` → `/data` | 升级容器不丢流 |
| 容器→后端 `:8000` | ✅ 可达 | `curl /api/v1/ping` 返回 `{"code":0,...}`（注意业务成功码是 **0**，非 200） |
| 容器→PG `:5432` | ✅ 可达 | |
| 容器→Redis `:6379` | ✅ 可达 | |
| `host.docker.internal` | ❌ 未定义 | 容器内访问宿主服务用 IP `192.168.80.136` 或网关 `172.18.0.1` |

**结论**：Node-RED 目前是「裸奔空壳」，但网络位置极好——它能直接调后端 API、连 PG、连 Redis，集成成本低。当前最大问题是**安全裸奔**（1880 无鉴权）和**零节点零流**（无实际用途）。

### 1.1 与后端集成的两个关键约束

- **WS 推送只有 4 个 topic**：`data` / `device` / `alarm` / `control`（[ws_hub.cpp:25](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/ws/ws_hub.cpp)），对应 Redis 频道 `ibmcp:ws:data` 等。Node-RED 可通过 WS 客户端连 `/ws/push`，或用 Redis 节点订阅这些频道。
- **数据接口只读**：`/api/v1/data/history`、`/api/v1/data/latest` 都是 GET；**没有外部遥测写入接口**。但有控制写入 `POST /api/v1/control/write`。→ Node-RED 要给 IBMCP 喂数据，最干净的路径是**转成 MQTT 发布，让后端 MQTT 驱动订阅**（复用现有采集链路），而不是直接写库。

### 1.2 后端 Modbus 地址规则（模拟器设计依据）

[modbus_address.h](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/collector/src/drivers/modbus_address.h) 与 [modbus_address.cpp](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/collector/src/drivers/modbus_address.cpp)：

- Modicon 记法 `<area>x<offset>`：`0`=线圈(FC01/05)、`1`=离散输入(FC02)、`3`=输入寄存器(FC04)、`4`=保持寄存器(FC03/06)；纯数字默认按保持寄存器。
- `offset` 是 **1-based**，解析后减 1 得到 libmodbus 的 0-based `register_index`。
- `float`/`double` 占 **2 个连续寄存器**。
- 采集器会把同一设备的点位合并成**一次批量保持寄存器读取**（[modbus_tcp_driver.cpp](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/collector/src/drivers/modbus_tcp_driver.cpp)）。

### 1.3 当前状态（阶段 0/1 完成后，2026-11-26 实测）

§1 表格是初始基线（裸奔空壳）。完成阶段 0 安全加固 + 阶段 1 模拟器落地后，当前状态：

| 项 | 状态 | 说明 |
|----|------|------|
| 第三方节点 | ✅ 已装 | `node-red-contrib-modbus@5.62.1`，位于容器 `/data/node_modules`，随 `package.json` 入库可复现 |
| 鉴权 | ✅ 已启用 | `adminAuth` 用户 `admin` / 口令 `ibmcp2026`（见 §1.4），编辑器/admin API 无 token 返回 HTTP 401 |
| 凭证密钥 | ✅ 已设置 | `credentialSecret` 固定值，容器重建/迁卷不丢加密凭证 |
| `host.docker.internal` | ✅ 已加 | compose 已加 `extra_hosts: ["host.docker.internal:host-gateway"]` |
| 业务流 | ✅ 已部署 | Modbus TCP 模拟器流监听 `0.0.0.0:1502`，覆盖车床+加工中心两台设备 |
| 端到端链路 | ✅ 打通 | Node-RED → 后端采集 → PG 入库 → Redis WS 推送 → 前端实时曲线，全部 Good |
| 物料入库 | ✅ 已提交 | 流/配置/palette 清单已纳入 [`deployments/nodered/`](../../deployments/nodered/)，`sync.sh` 一键复现 |

### 1.4 账号与口令

Node-RED 集成涉及**两套相互独立**的账号体系（不做 SSO 穿透，避免越权）：

| 用途 | 用户名 | 口令 | 存储位置 | 说明 |
|------|--------|------|----------|------|
| 主系统后端（前端登录 + 容器控制 API） | `admin` | `admin123` | 迁移 [`0002_rbac_sys_users.sql`](../../backend/migrations/0002_rbac_sys_users.sql) 播种的 `sys_users`（argon2id 哈希） | 登录 IBMCP 前端、调 `/api/v1/node-red/*` 等后端 API；engineer 等其它账号见「系统管理 → 用户管理」 |
| Node-RED 编辑器 | `admin` | `ibmcp2026` | 容器 `/data/settings.js` 的 `adminAuth`（bcrypt 哈希，见 [`settings.js.patch`](../../deployments/nodered/settings.js.patch)） | iframe 内 `/nodered/` 登录；与主系统 JWT 无关，进入页面后需单独登录一次 |

> ⚠️ 以上为**联调环境**口令，仅用于当前环境。**生产环境必须重新生成哈希并移出公开仓库**（`settings.js.patch` 与 [README](../../deployments/nodered/README.md) 「安全说明」已注明），切勿直接复用。
>
> 改密方式：
> - Node-RED 编辑器：容器内 `node-red admin hash-pw` 生成 bcrypt 哈希 → 替换 `/data/settings.js` 中 `adminAuth.users[].password` → `sudo docker restart ibmcp-nodered`。
> - 主系统 admin：前端「系统管理 → 用户管理」或 `PUT /api/v1/auth/users/{id}/password`。

---

## 2. 定位与边界（最重要的一节）

Node-RED **不是** IBMCP 的采集核心，而是**编排 / 胶水 / 调试 / 扩展层**。边界必须划清，否则会重复造轮子、引入性能与一致性问题。

```
┌──────────────────────────────────────────────────────────────────┐
│  核心数据面（C++ 后端 ibmcp_server，Node-RED 不碰这层）              │
│    协议驱动采集 → 200ms 采样泵 → 批量 COPY 入库 → 告警判定           │
│    → WS 四主题推送（data/device/alarm/control）                    │
│    强一致 · 高吞吐 · 低延迟                                         │
├──────────────────────────────────────────────────────────────────┤
│  编排/扩展/调试面（Node-RED 主场）                                   │
│    设备模拟 · 协议适配 · 事件联动 · 数据导出 · 临时看板               │
│    灵活 · 可视化编排 · 免编译 · 秒级动作                             │
└──────────────────────────────────────────────────────────────────┘
```

**该用 Node-RED 的**：没有真实硬件时的联调、异构协议快速接入、告警通知与闭环、数据对外对接、临时可视化——这些需求变化快、用 C++ 改造成本高。

**不该用 Node-RED 的**：替代采集器做高频采集（Node-RED 是单线程 JS，做不了 200ms 稳定批量采样）、替代 PG 做数据存储、做核心控制安全回路（高可靠控制必须在后端/PLC 层）。

---

## 3. 集成拓扑

```
                 ┌──────────────────────────────────────────────┐
                 │            Node-RED 容器 (1880)               │
                 │  ┌─────────────┐  ┌──────────────────────┐   │
   模拟设备 ─────►│  │ Modbus/OPCUA│  │  编排流              │   │
   (无硬件)       │  │ Server 节点 │  │  inject→function→... │   │
                 │  └──────┬──────┘  └───┬──────────┬───────┘   │
                 └─────────┼─────────────┼──────────┼───────────┘
                           │ :1502        │ WS/订阅   │ HTTP
                           ▼             ▼          ▼
        ┌───────────────────────────────────────────────────────┐
        │  后端 ibmcp_server (裸机 :8000)                         │
        │   采集器 ──读 Modbus :1502──► 入库 PG ──► WS 推送 ──► 前端│
        │   ◄── 调 /control/write ──  Node-RED 可发控制命令        │
        └──────────────┬───────────────┬─────────────────────────┘
                       │ :5432         │ :6379
                       ▼               ▼
                  PostgreSQL        Redis (ibmcp:ws:* 频道)
```

Node-RED 与后端有三条集成通道：
1. **被采集**：Node-RED 当 Modbus/OPC UA/MQTT 服务器，后端采集器连它（场景 1）。
2. **订阅事件**：Node-RED 连后端 WS `/ws/push` 或订阅 Redis `ibmcp:ws:*`（场景 2）。
3. **主动调用**：Node-RED 调后端 HTTP API（控制写入 `/api/v1/control/write`、读数据等）（场景 2/3）。

---

## 4. 场景设计

### 4.1 场景一：设备模拟器 / 联调沙盒（首个落地）

**目的**：没有真实 PLC/仪表时，用 Node-RED 模拟设备，跑通 `采集→入库→WS推送→前端展示` 全链路；也用于采集器回归测试。

**要装的节点**：`node-red-contrib-modbus`（5.62.1，兼容 NR 5.x / Node ≥22，已核实）。

**流设计**：

```
[Inject 1s] ──► [Function: 生成模拟量] ──► [Modbus Write: 写保持寄存器]
                                                  │
                                                  ▼
                                       [Modbus Server: 监听 0.0.0.0:1502, unit id 1]
                                                  ▲
                                                  │ 后端采集器 modbus_read_registers (FC03) 读取
```

**寄存器映射（保持寄存器 area 4，后端大端 ABCD float，实测 v2 双设备）**：

流 `func-gen` 维护 52 字节缓冲，1s 注入批写，覆盖数控车床 + 加工中心两台设备：

| register_index | 后端点位地址 | 设备 | 信号 | 数据类型 | 模拟值 |
|---------------|-------------|------|------|---------|--------|
| 0–1 | `4x0001` | 数控车床 CNC-LATHE-01 | 主轴转速 spindle_speed | float | 3000 ± 600 sin |
| 2–3 | `4x0003` | 数控车床 CNC-LATHE-01 | 主轴负载 spindle_load | float | 45 ± 35 sin |
| 4–5 | `4x0005` | 数控车床 CNC-LATHE-01 | 冷却液温度 coolant_temp | float | 38 ± 12 sin（故障→75） |
| 6   | `4x0007` | 数控车床 CNC-LATHE-01 | 运行状态 run_status | uint16 | 1（运行），故障→2 |
| 7   | `4x0008` | 数控车床 CNC-LATHE-01 | 急停 emergency_stop | bool | 0 |
| 10–11 | `4x0011` | 加工中心 MACHINING-01 | 进给速度 feed_rate | float | 1600 ± 400 sin |
| 20–21 | `4x0021` | 加工中心 MACHINING-01 | 有功功率 power_active | float | 14 ± 6 sin |
| 22–23 | `4x0023` | 加工中心 MACHINING-01 | 主轴负载 spindle_load | float | 50 ± 30 sin |
| 24  | `4x0025` | 加工中心 MACHINING-01 | 运行状态 run_status | uint16 | 1 |
| 25  | `4x0026` | 加工中心 MACHINING-01 | 急停 emergency_stop | bool | 0 |

> ⚠️ **125 寄存器批量读约束（实测踩坑）**：后端采集器把同一设备的点位合并成**一次** FC03 批量读，Modbus 单次读上限 125 寄存器。同设备所有点位地址跨度若超过 125，整批读失败 → 全部点位回退到内置模拟值（quality 可能仍标 Good，但值是假的）。原始测试数据车床状态点位在 `4x1001`（idx1000），与 `4x0001`（idx0）跨度 1003，导致整批失败；已把状态/急停挪到 `4x0007`/`4x0008` 修复。映射时务必让同设备点位落在连续 125 寄存器内。

> 模拟器提供**全量重置**能力（`复位` 注入：所有寄存器归零 + 清故障），便于回归测试基线对齐；`置故障` 注入把冷却液温度打到 75、运行状态置 2。

**后端对接**：`running_route_device` 的 `device_connection` 填 `{"host":"127.0.0.1","port":1502,"slave_id":1}`（后端是裸机服务，与容器同宿主，用 `127.0.0.1` 即可），`protocol_code` 填 `MODBUS_TCP`。点位地址按上表填 `4x0001` 等。**改完 DB 配置后不要只重启后端**，必须调 `POST /api/v1/collector/reload`（admin 角色）触发计划重编译——见 §8 注意事项 8。

**验证（脚本化，不靠肉眼）**：
- `curl /api/v1/runtime/devices/{id}/points` 看点位值在变；
- `curl /api/v1/data/latest` 确认入库；
- `redis-cli psubscribe 'ibmcp:ws:data'` 看 WS 推送频道有消息；
- 前端监控页看曲线。

### 4.2 场景二：告警 / 事件联动编排

**目的**：把后端的实时告警/事件「导出」成外部动作，规则可随时改、无需编译后端。

**接入方式**（二选一）：
- **WS 客户端**：Node-RED 用 websocket 节点连 `ws://192.168.80.136:8000/ws/push`，按 topic 过滤 `alarm`。
- **Redis 订阅**（推荐，解耦更好）：装 `node-red-contrib-redis` 或内置 redis 节点，`SUBSCRIBE ibmcp:ws:alarm`，不占后端 WS 连接。

**典型流**：

```
[redis subscribe ibmcp:ws:alarm] ──► [function: 解析告警级别/设备]
       ├─► [http request: 调钉钉/企微 webhook]   (严重告警)
       ├─► [email node: 发邮件]                   (一般告警)
       └─► [http request: POST /api/v1/control/write]  (自动闭环：如超温停机)
```

**注意**：`/api/v1/control/write` 需 Bearer Token（非 public 路径见 [config.json public_paths](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/config.json)），Node-RED 里要先登录拿 token 并处理过期刷新。建议后端为 Node-RED 开一个**服务账号**（固定角色、最小权限），不要用个人账号 token。

### 4.3 场景三：外部协议适配 / 数据接入网关

**目的**：遇到后端采集器暂不支持的协议/设备时，Node-RED 做协议转换快速接入。

**接入路径**（按推荐度）：
1. **MQTT 桥接（推荐，零后端改动）**：Node-RED 收原始数据 → 清洗 → 发布到 MQTT broker → 后端 MQTT 驱动订阅。需要一个 MQTT broker（可由 Node-RED 装 `node-red-contrib-aedes` 内嵌，或单独起 mosquitto 容器）。
2. **HTTP 上报（需小开发）**：后端新增 `POST /api/v1/data/report` 接口接收遥测 JSON，Node-RED 调它。适合数据量小、需要后端业务校验的场景。
3. **直写 PG（不推荐）**：绕过业务层，仅适合一次性数据补录/导入，且必须走与后端相同的表结构和写入约束。

### 4.4 场景四：数据导出 / 临时看板

**目的**：IBMCP 数据对外对接，或现场快速可视化（不改 Vue 前端）。

- **导出**：装 `node-red-contrib-postgresql`，定时查 PG → 推 InfluxDB / 第三方 BI / 生成报表文件。
- **临时看板**：装 `node-red-dashboard`，做曲线/开关/表格，给产线或调试用。生产正式看板仍走 Vue 前端，Node-RED dashboard 只做临时/调试用途。

---

## 5. 安全加固（生产上线前必做）

当前 1880 裸奔是最大风险：进编辑器即可改流，甚至能加 `exec` 节点在容器内执行命令。按以下顺序加固：

1. **加 `credentialSecret`**：在 `/data/settings.js` 设固定密钥（容器重建/迁卷不丢加密凭证）。
2. **开 `adminAuth`**：给编辑器加用户名/密码（用 `node-red admin hash-pw` 生成密码哈希）。
3. **nginx 收口**：把 1880 收进 nginx（加路径前缀如 `/nodered`，带 Basic Auth 或复用后端鉴权），**不再对公网直接暴露 1880**。生产甚至可只允许运维 IP 访问。（`/nodered/` 同源反代已落地，前端 iframe 嵌入，见 §10）
4. **容器隔离**：Node-RED 容器不应挂载宿主敏感目录；`exec` 节点能力评估后决定是否在 settings 里禁用（`editorTheme.palette.excludes`）。
5. **加 `extra_hosts`**：在 [docker-compose.yml](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/deployments/openeuler/docker-compose.yml) 给 nodered 服务加 `extra_hosts: ["host.docker.internal:host-gateway"]`，让流里能用稳定的 `host.docker.internal` 访问后端，而不是写死 IP。

---

## 6. Node-RED 物料版本管控

当前流和配置只存在于 docker 卷里，**不进版本控制**，换机器/重建就丢。建立仓库内物料目录：

```
deployments/nodered/
├── README.md              # 本目录说明
├── flows/
│   └── simulator-modbus.json   # 导出的模拟器流（场景一）
├── package.json           # palette 清单（装了哪些节点+版本），npm install 可复现
├── settings.js.patch      # settings.js 的定制片段（adminAuth/credentialSecret）
└── sync.sh                # 把 flows/package.json 同步进运行中容器的 /data 并重启
```

落地流程：在运行容器里调好流 → 导出 → 提交到 `deployments/nodered/flows/` → `sync.sh` 可在新机器一键还原。让模拟器可复现、可评审，而不是「只存在于某个容器里」。

---

## 7. 落地路线图

| 阶段 | 内容 | 改后端？ | 产出 | 状态 |
|------|------|---------|------|------|
| **0. 武装 Node-RED** | 装 modbus 等节点、开 adminAuth、设 credentialSecret、compose 加 extra_hosts | 否 | 安全可用的 Node-RED | ✅ 已完成 |
| **1. 设备模拟器** | 建 Modbus TCP Server 流（寄存器映射见 §4.1），后端建路由点位指向它，脚本化验证全链路，导出入库 | 否（仅配置数据） | 端到端演示链路 + `deployments/nodered/` 物料 | ✅ 已完成 |
| **2. 告警联动** | 订阅 `ibmcp:ws:alarm` → 发通知（钉钉/邮件优先） | 否 | 告警自动通知 | 按需 |
| **3. 扩展** | 数据接入网关 / 导出 / dashboard | 场景3 可能加 1 个上报接口 | 按需求 | 按需 |

---

## 8. 注意事项与约束

1. **业务成功码是 0**：后端 `/api/v1/ping` 返回 `{"code":0,...}`，Node-RED 里判断成功要用 `code === 0`，不要按 HTTP 200 或 code 200 判断。
2. **访问宿主用 IP**：容器内 `localhost` 指容器自己；访问后端/PG/Redis 用 `192.168.80.136`，或加了 `extra_hosts` 后用 `host.docker.internal`。
3. **WS 鉴权**：`/ws/push` 是免鉴权路径（[main.cpp:58](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/main.cpp)），Node-RED 订阅 WS 无需 token；但调控制/配置 API 需要 token。
4. **性能边界**：Node-RED 适合秒级~百毫秒级动作；不要用它做高频采集或核心控制回路。
5. **凭证迁移**：设了 `credentialSecret` 后，迁卷（07 文档提到的 `ibmcp_nodered_data` 迁移）时密钥必须一致，否则 `flows_cred.json` 解不开。
6. **故障注入要可重置**：模拟器/故障注入流必须提供全量重置入口（寄存器归零 + 状态清除），保证测试基线可重复（经验教训）。
7. **验证要脚本化**：每个场景配一条可重复执行的验证命令/脚本，不靠 GUI 肉眼判断流是否跑通（经验教训）。
8. **改配置后必须调 reload，不能只重启后端**（本次最大的坑）：`main.cpp` 启动时的 `bootstrap()` 以分离协程 fire-and-forget 启动，协程在首个 `co_await` 挂起后被销毁，所以**重启服务不会重编译计划**。改完 `running_route_device` / `running_device_collection` 后，必须调 `POST /api/v1/collector/reload`（需 admin 角色），它内部正确 `co_await orchestrator().bootstrap()` 从 DB 重编译并应用。证据：reload 后日志出现 `CollectorOrchestrator.bootstrap revision=N devices=2`，单纯重启没有这些行。
9. **同设备点位地址跨度 ≤ 125 寄存器**：采集器把同设备点位合并成一次 FC03 批量读，Modbus 单次读上限 125；超跨度整批失败 → 全部回退内置模拟值。详见 §4.1 警告框。
10. **临时 admin 用户 `simadmin`**：为调 reload API 临时创建（密码 `sim@2026`，roles={admin}，id=6），仅用于联调。**联调结束应删除**：`DELETE FROM sys_users WHERE username='simadmin';`。生产请勿在 DB 里留弱口令临时账号。
11. **前端监控设备树字段名**：前端 `DeviceTree.vue` 曾读 `routes[].bindings`，但后端 `equipmentExpand` 返回的是 `routes[].devices`，导致监控页设备树展开为空、看不到曲线。已改为 `r.devices` 并重新部署前端。若换前端版本后设备树又空，先检查这个字段是否一致（后端契约是 `devices`，`ConfigEditView`/`ConfigDetailView` 都读 `devices`）。

---

## 9. 与现有文档的关系

| 主题 | 文档 |
|------|------|
| 整体架构、Node-RED 在拓扑中的位置 | [01-Drogon 架构设计](./01-Drogon架构设计.md) |
| 后端 WS 推送 topic、Redis 频道 | [09-Redis 缓存与实时推送](./09-Redis缓存与实时推送.md) |
| 欧拉部署、docker-compose、数据卷迁移 | [05-openEuler 构建与部署](./05-openEuler构建与部署.md)、[07-跨机器迁移与离线交付方案](./07-跨机器迁移与离线交付方案.md) |
| 全项目技术栈与端口 | [10-全项目技术栈与部署指南](./10-全项目技术栈与部署指南.md) |

---

## 10. 前端集成：菜单入口 + iframe 嵌入 + 容器控制条（2026-11-26 已落地）

在前端「运行配置」菜单下新增「Node-RED 流编辑器」入口，把 Node-RED 编辑器通过 iframe 嵌入前端显示界面，并提供容器控制条（启动/停止/重启 + 实时状态轮询）。

### 10.1 产品决策

- **嵌入方式 = nginx 同源反代**（非 iframe 直连 1880）：Node-RED 设 `httpAdminRoot: '/nodered'`，nginx `location /nodered/` → `127.0.0.1:1880`（透传前缀 + WS Upgrade），iframe `src="/nodered/"`。同源避免跨域 / CSP 问题，也不再直接暴露 1880 端口。
- **控制范围 = 完整控制条（admin 操作 / engineer 只读）**：状态轮询 + 启动/停止/重启按钮；admin 可见并可操作，engineer 可见状态与编辑器但无操作按钮。

### 10.2 架构链路

```
浏览器 iframe src="/nodered/"
   └─ nginx location /nodered/  →  127.0.0.1:1880  (透传前缀, 含 /nodered/comms WS)
        └─ Node-RED 容器 ibmcp-nodered (httpAdminRoot='/nodered', adminAuth 已开)
```

配套变更：

- Node-RED：[settings.js.patch](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/deployments/nodered/settings.js.patch) 加 `httpAdminRoot: '/nodered'`（设后根 `/` 变 404 属正常，编辑器在 `/nodered/`；Modbus 采集 1502 不受影响）。
- nginx：[packaging/nginx/ibmcp.conf](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/packaging/nginx/ibmcp.conf) 新增 `location /nodered/` 反代块（含 `Upgrade` / `Connection` 头与 3600s 超时，支撑编辑器 WS `/nodered/comms`）。
- 后端：[node_red_controller.h](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/include/controller/node_red_controller.h) / [.cpp](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/controller/node_red_controller.cpp) 提供 4 个 API；[shell_util.h](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/include/util/shell_util.h) 封装 `sudo -n docker` 命令执行（`popen`）与 TCP 可达性探测（非阻塞 connect + select 超时）。**所有命令均为常量字符串，不接收用户输入拼接**。
- 前端：[api/nodeRed.ts](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/frontend/src/api/nodeRed.ts)、[views/node-red/NodeRedEditor.vue](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/frontend/src/views/node-red/NodeRedEditor.vue)；路由 `/node-red/editor` 挂在 MainLayout children，菜单加在「运行配置」submenu；[vite.config.ts](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/frontend/vite.config.ts) dev 代理加 `/nodered` → `VITE_NODERED_TARGET || http://127.0.0.1:1880`。

### 10.3 后端 API

| 方法 | 路径 | 鉴权 | 说明 |
|------|------|------|------|
| GET | `/api/v1/node-red/status` | 任意登录用户 | 返回 `{container, editorReachable, updatedAt}` |
| POST | `/api/v1/node-red/start` | admin | `sudo -n docker start ibmcp-nodered` |
| POST | `/api/v1/node-red/stop` | admin | `sudo -n docker stop -t 5 ibmcp-nodered` |
| POST | `/api/v1/node-red/restart` | admin | `sudo -n docker restart -t 5 ibmcp-nodered` |

- `container` 来自 `docker inspect -f '{{.State.Status}}'`（`running` / `exited` / `missing`）。
- `editorReachable` 是后端对 `127.0.0.1:1880` 的 TCP 探测（非容器状态），能反映编辑器是否真正就绪（容器刚启动时可能为 false，几秒后变 true，前端 5s 轮询会刷新）。
- 鉴权：`status` 用 `auth::requireAuth`，写操作用 `ctrl::requireAnyRole(req, {"admin"})`，非 admin 抛 `BizError(403)`，与 `/api/v1/collector/reload` 一致。
- 注意：这是容器控制，**不涉及采集计划**，改完无需调 `collector/reload`（区别于 §8.8）。

### 10.4 实测结论（2026-11-26）

- 后端 API：admin（口令 `admin123`，播种于迁移 `0002`）登录后，status → `running` / `editorReachable=true`；stop → `exited` / `false`；start → `running` / `true`；restart → `running`；临时建的 engineer 调 stop 返回 `code=403`、调 status 返回 `code=0`，测试用户已删除。
- 前端：硬刷新后「运行配置」出现「Node-RED 流编辑器」；控制条显示状态标签（运行中 · 编辑器可用）与启动/停止/重启三按钮（运行中时启动按钮禁用）；iframe 渲染出完整 Node-RED 编辑器（标题 `Node-RED : IBMCP PLC Simulator`，含画布 / 调色板 / 侧边栏 / 部署按钮）。
- 账号体系：两套独立账号（主系统后端 JWT 与 Node-RED `adminAuth`），用户名/口令及改密方式详见 §1.4；iframe 内需单独登录一次 Node-RED。
