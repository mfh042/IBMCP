# deployments/nodered — Node-RED 物料版本管控

本目录把运行在欧拉容器 `ibmcp-nodered` 里的 Node-RED 流与配置纳入版本控制，
做到**可复现、可评审、换机器/重建不丢**。详细背景见
[docs/0-Drogon/11-Node-RED集成方案.md](../../docs/0-Drogon/11-Node-RED集成方案.md)。

## 目录内容

| 文件 | 说明 |
|------|------|
| `flows/simulator-modbus.json` | 场景一「Modbus TCP 设备模拟器」导出的流（权威版本，从运行容器 `/data/flows.json` 导出） |
| `package.json` | 容器 `/data/package.json`，记录已装 palette 节点及版本（`npm install` 可复现） |
| `settings.js.patch` | 对 Node-RED 默认 `settings.js` 的三处定制：`credentialSecret`、`adminAuth` 与 `httpAdminRoot` |
| `sync.sh` | 把本目录的 `flows.json` + `package.json` 同步进运行中容器并重启的一键脚本 |

> 后端裸机 systemd 服务 `ibmcp-api`、PostgreSQL、Redis 不在此目录；
> compose 见 [`deployments/openeuler/docker-compose.yml`](../openeuler/docker-compose.yml)。

## 一键还原（新机器 / 容器重建后）

在能访问目标 docker 主机的地方执行：

```bash
cd deployments/nodered
bash sync.sh          # 需要 sudo docker 权限
```

`sync.sh` 会：
1. `docker cp flows/simulator-modbus.json → ibmcp-nodered:/data/flows.json`
2. `docker cp package.json → ibmcp-nodered:/data/package.json`
3. 容器内 `cd /data && npm install --omit=dev` 安装 palette 节点
4. `docker restart ibmcp-nodered`

`settings.js` 的三处定制（`credentialSecret` / `adminAuth` / `httpAdminRoot`）已在当前容器里落地并备份在
`/home/meng/nodered-backup/`；新机器首次部署时按 `settings.js.patch` 手改一次即可
（`credentialSecret` 一旦设定不可更改，否则 `flows_cred.json` 解不开；`httpAdminRoot: '/nodered'` 必须与 nginx 反代前缀一致）。

## 访问

- 前端入口：登录 IBMCP 前端 →「运行配置 → Node-RED 流编辑器」，编辑器以 iframe 同源嵌入（`src="/nodered/"`），并带容器控制条（状态 + 启动/停止/重启，仅 admin 可操作，engineer 只读）。
- 经 nginx 直接访问：`http://192.168.80.136/nodered/`（同源反代到容器 1880；已启用 `adminAuth`，Node-RED 侧用户 `admin`）。
- 容器直连（仅调试）：`http://192.168.80.136:1880/nodered/`（生产不应对公网暴露 1880）。
- Modbus TCP 从站：`192.168.80.136:1502`（后端采集器连接此端口，与 `httpAdminRoot` 无关）。

## 寄存器映射（保持寄存器 area 4，后端大端 ABCD float）

流 `func-gen` 维护一个 52 字节缓冲，1s 注入一次批写保持寄存器，覆盖两台设备：

| idx | 地址 | 设备 | 信号 | 数据类型 | 模拟值 |
|-----|------|------|------|---------|--------|
| 0–1 | `4x0001` | 数控车床 CNC-LATHE-01 | 主轴转速 spindle_speed | float | 3000 ± 600 sin |
| 2–3 | `4x0003` | 数控车床 CNC-LATHE-01 | 主轴负载 spindle_load | float | 45 ± 35 sin |
| 4–5 | `4x0005` | 数控车床 CNC-LATHE-01 | 冷却液温度 coolant_temp | float | 38 ± 12 sin（故障注入→75） |
| 6   | `4x0007` | 数控车床 CNC-LATHE-01 | 运行状态 run_status | uint16 | 1（运行），故障→2 |
| 7   | `4x0008` | 数控车床 CNC-LATHE-01 | 急停 emergency_stop | bool | 0 |
| 10–11 | `4x0011` | 加工中心 MACHINING-01 | 进给速度 feed_rate | float | 1600 ± 400 sin |
| 20–21 | `4x0021` | 加工中心 MACHINING-01 | 有功功率 power_active | float | 14 ± 6 sin |
| 22–23 | `4x0023` | 加工中心 MACHINING-01 | 主轴负载 spindle_load | float | 50 ± 30 sin |
| 24  | `4x0025` | 加工中心 MACHINING-01 | 运行状态 run_status | uint16 | 1 |
| 25  | `4x0026` | 加工中心 MACHINING-01 | 急停 emergency_stop | bool | 0 |

> **125 寄存器约束**：后端采集器把同一设备的点位合并成一次批量保持寄存器读（FC03），
> Modbus 单次读上限 125 寄存器。同设备所有点位地址必须落在连续 125 寄存器内，
> 否则整批读失败 → 全部回退内置模拟值。本映射车床跨度 idx0–7、加工中心跨度 idx10–25，均合规。

流还提供两个手动注入：`置故障`（把冷却液温度打到 75、运行状态置 2）和 `复位`
（所有寄存器归零 + 清故障），用于回归测试基线对齐。

## 后端对接（已在欧拉 DB 配置）

`running_route_device` 里两台设备的绑定均指向：

```json
{"host": "127.0.0.1", "port": 1502, "slave_id": 1}
```

（后端是裸机服务，与容器同宿主，用 `127.0.0.1` 即可，无需走宿主 IP。）

改完 DB 配置后**不要只重启后端**——必须调
`POST /api/v1/collector/reload`（admin 角色）触发计划重编译，详见文档 11 §8。

## 安全说明（生产前必看）

- `credentialSecret` 与 `admin` 密码哈希已随本目录入库，仅用于当前联调环境；
  **生产环境必须重新生成并移出公开仓库**（用环境变量或独立 secret 管理）。
- `adminAuth` 密码用 `node-red admin hash-pw` 生成；如需改密，重新生成哈希替换 `settings.js`。
- 已把编辑器收进 nginx 同源反代（`/nodered/` → `127.0.0.1:1880`，含 WS Upgrade），前端 iframe 同源嵌入，不再依赖直接暴露 1880；生产仍建议在防火墙层限制 1880 仅本机/运维网段访问。
