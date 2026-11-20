# IBMCP 接口契约与功能对照（FastAPI 版）

>
版本：v1.0　　编写日期：2026-11-18　　配套：[01-架构设计](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/01-IBMCP-FastAPI重构架构设计.md) · [02-数据模型与字段契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/02-数据模型与字段契约（FastAPI版）.md)
>
> **本文档定位**：把 `开发指南/03-API接口及测试用例` 的 **118 个 REST + 1 个 WebSocket**，与 `开发指南/02.02-Vue前端` 的
> **四大功能模块**，按 FastAPI 重构后的形态重新落位。回答三件事：
> 1. 每个旧接口在 FastAPI 版**变成什么**（保留 / 改写 / 拆分 / 废弃）；
> 2. 前端**四大模块**依赖的关键功能点在新的接口层怎么落地；
> 3. WebSocket 的**四固定主题**在新架构下的**载荷契约**（与旧版 payload 的差异）。
>
> 本文档是**接口层开发的唯一验收清单**，与旧文档冲突时以本文档为准。

---

## 一、接口总览：从 118 个单体路由到分层路由

### 1.1 规模对照

| 维度           | 旧（历史方案 `main.cpp` 注册） | 新（FastAPI Router）                                                 |
|----------------|--------------------------------|----------------------------------------------------------------------|
| REST 路由数    | 118                            | **约 96**（合并字典六件套 + 拆分配置链路）                           |
| WebSocket 端点 | 1（`/ws/push`，全局广播）      | **1**（`/api/v1/ws/collect`，四固定主题 `data`/`device`/`alarm`/`control`） |
| 路由注册方式   | 手写 lambda 逐个绑定           | `StaticDictRouter` 工厂批量生成                                      |
| 鉴权           | 无强制拦截（仅具备能力）       | **Depends 强制依赖**，白名单端点除外                                 |
| 响应包装       | `Result::ok / fail` 手动       | `response_model` 统一中间件                                          |
| 入参校验       | 手写 `if (!json[...])`         | Pydantic 声明式                                                      |

> **合并收益**：4 个字典 × 6 操作 = 24 个旧路由，由 1 个 `StaticDictRouter` 工厂一次性生成，新增字典 0 行路由代码。

### 1.2 路由分层（`app/api/v1/`）

| 协议前缀              | 职责                                  | 依据                                                                                    |
|-----------------------|---------------------------------------|-----------------------------------------------------------------------------------------|
| `/api/v1/auth`        | 登录 / Token / 用户 / 角色            | 旧 AuthController                                                                       |
| `/api/v1/static/*`    | 10 张静态表 CRUD                      | 旧 DictController + EquipmentController + BaseDeviceController + ProcessRouteController |
| `/api/v1/config/*`    | 五级配置链 CRUD + 激活/停用           | 旧 EquipmentConfigController + ConfigController                                         |
| `/api/v1/runtime/*`   | 运行态查询（设备、点位、命令、快照）  | 旧 DeviceController 的读侧                                                              |
| `/api/v1/data/*`      | 历史时序查询                          | 旧 DataController                                                                       |
| `/api/v1/alarms/*`    | 报警列表 / ack / recover              | 旧 AlarmController                                                                      |
| `/api/v1/control/*`   | 控制下发（异步回执模型）              | 旧 DeviceController 的控制侧                                                            |
| `/api/v1/collector/*` | 扩展探针（协议清单、健康、手动重放）  | **新增**                                                                                |
| `/api/v1/ws/collect`  | 实时数据 / 设备状态 / 报警 / 控制回执 | 旧 `/ws/push`                                                                           |

### 1.3 兼容策略：旧路径是否保留

**结论：不保留 `/api/*` 旧路径别名。**

理由：前端（Vue）与后端在本项目内同步演进，无第三方调用方；保留双路径会长期背负两套响应格式（旧 `{code,message,data}` vs
新中间件包装），后面必然分裂。前端统一把 `comm/http.ts` 的 baseURL 改为 `/api/v1`，一次性切换。

> 若确有外部系统对接，可在 Nginx 层写 `/api/(old)` → `/api/v1/(new)` 的 `rewrite` 规则，**不动应用代码**（见主文档部署章节）。

---

## 二、统一响应与通用约定（继承 + 强化）

### 2.1 统一响应结构（保持旧版契约，前端无需换解析）

```json
{ "code": 0, "message": "ok", "data": ..., "trace_id": "01HXXX" }
```

| 字段       | 类型   | 说明                                            |
|------------|--------|-------------------------------------------------|
| `code`     | int    | `0` 成功；其余为业务码（见 2.3）                |
| `message`  | string | 人类可读                                        |
| `data`     | any    | 业务数据                                        |
| `trace_id` | string | **新增**，请求级追踪 ID，与日志 `trace_id` 一致 |

**实现方式**：`ResponseEnvelopeMiddleware` + `response_model` 双保险 —— 路由返回**纯 Pydantic 模型**，由中间件统一包成信封，路由内部
**不再手写 `Result::ok`**（旧版 118 处手写包装，是最大的维护负担）。

> **关键约束**：HTTP 状态码语义与旧版**保持一致**（业务失败仍返回 HTTP 200 + `code≠0`），只有"请求体非法 JSON / 鉴权失败 /
> 路由不存在"才用真实 4xx/5xx。这样前端 `comm/http.ts` 的拦截器逻辑完全不用改。

### 2.2 HTTP 状态码映射（强化版）

| 场景                       | HTTP    | `code`                            |
|----------------------------|---------|-----------------------------------|
| 成功                       | 200     | 0                                 |
| Pydantic 校验失败          | **422** | 422                               |
| 未认证 / Token 失效        | **401** | 401                               |
| 已认证但无角色权限         | **403** | 403                               |
| 请求体非 JSON              | 400     | 400                               |
| 业务参数错误               | 200     | 400                               |
| 资源不存在                 | 200     | 404                               |
| 唯一键冲突                 | 200     | 409                               |
| 数据库异常                 | 200     | 500                               |
| 扩展拒绝 plan / 驱动不可用 | 200     | **4001 / 4002**（新增扩展业务码） |

> **变化点**：`422` 与 `401/403` 从"业务码"升级为"真实 HTTP 状态码"，因为它们是**框架层**判定（Pydantic / `HTTPBearer`
> ），中间件无法也不该把它们降级成 200。前端拦截器需新增对 HTTP 401 的处理（跳登录），其余保持旧逻辑。

### 2.3 业务码段位分配（新增，解决旧版码值散乱）

| 段位        | 含义                                     | 示例                                                                                                       |
|-------------|------------------------------------------|------------------------------------------------------------------------------------------------------------|
| `0`         | 成功                                     | —                                                                                                          |
| `4xx`       | 通用客户端错误                           | 400 参数、404 不存在、409 冲突                                                                             |
| `5xx`       | 通用服务端错误                           | 500 DB、503 扩展不可用                                                                                     |
| `1xxx`      | Auth / RBAC                              | 1001 密码错误、1002 用户禁用、1003 角色不存在                                                              |
| `2xxx`      | 静态配置                                 | 2001 编码重复、2002 引用不存在、2003 协议无驱动                                                            |
| `3xxx`      | 运行动态配置                             | 3001 配置未激活、3002 编译失败、3003 环路引用                                                              |
| `4001~4099` | **扩展返回码透传**（`code = 4000 + rc`） | 4001 协议不支持、4002 连接参数非法、4003 plan 版本冲突、4004 未初始化、4005 控制队列满、4006 目标不在 plan |
| `4100~`     | 控制                                     | 4101 点位只读、4102 命令超时、4103 设备离线、4104 控制功能未启用                                           |

> **扩展错误码必须原样透传**，不要改写成 500。运维排查时"扩展说什么"比"我们包装成什么"重要得多。
>
> **码值唯一来源是 05 号 8.1**：扩展侧只抛 6 个异常类并带 `rc`（1~6），Python 的 `to_api_error()` 计算 `code = 4000 + rc`
> 后直接写入响应体。本表 `4101~4104` 属于 FastAPI 侧自产的**控制类**业务码，与扩展无关（扩展只报 `rc=5` 队列满 / `rc=6` 不在
> plan）。

### 2.4 分页约定（保持旧版，前端零改动）

```json
{ "items": [...], "total": 128 }
```

公共 Query：`page`（默认 1）、`size`（默认 20，**上限 200**，新增约束）、`keyword`。

> **新增上限**：旧版无 size 上限，前端传 `size=100000` 会直接拖垮 DB。FastAPI 用 `Query(le=200)` 卡死。

### 2.5 鉴权（从"具备能力"到"强制生效"）

旧版最大安全缺口：**AuthController 已实现，但所有业务接口无中间件拦截**（03 号文档 0.6
明确写了"接口当前无强制中间件拦截"）。FastAPI 版改为：

```python
# app/api/deps.py
bearer = HTTPBearer(auto_error=False)

async def current_user(cred = Depends(bearer)) -> UserCtx: ...
async def require_roles(*roles: str) -> UserCtx: ...   # 工厂依赖
```

| 端点分组                                  | 权限要求          |
|-------------------------------------------|-------------------|
| `/auth/login`、`/auth/refresh`、`/health` | 白名单，无鉴权    |
| `/static/*` 读（list/all/get）            | `viewer` 及以上   |
| `/static/*` 写（create/update/delete）    | `engineer` 及以上 |
| `/config/*` 写 + 激活                     | `engineer` 及以上 |
| `/control/*` 下发                         | `engineer` 及以上 |
| `/alarms/*/ack`                           | `operator` 及以上 |
| `/auth/users`、`/auth/roles` 写           | `admin`           |
| `/api/v1/collector/*`                     | `admin`           |

**JWT 载荷**（比旧版多字段）：

```json
{ "sub": "42", "username": "admin", "roles": ["admin"], "exp": 1730000000, "iat": 1729996400 }
```

**登录响应补齐**（修 S8）：旧版缺 `id`，新版返回完整用户上下文：

```json
{ "token": "...", "refreshToken": "...", "id": 42, "username": "admin",
  "displayName": "系统管理员", "roles": ["admin"] }
```

### 2.6 CORS（改为受控白名单）

旧版 `Access-Control-Allow-Origin: *` 与 `Authorization` 同时使用时，浏览器视为非法组合的风险随部署形态变化。FastAPI 版走
`CORSMiddleware`，来源从 `settings.cors_origins` 读取（默认 `http://localhost:5173`，生产填前端域名）。**生产禁用 `*`**。

---

## 三、接口逐个落位（旧 118 路由 → 新路由映射表）

> 下表是**迁移工作表**，开发时按此表逐个实现并勾选。`旧路径` 均去掉 `/api` 前缀，新路径统一加 `/api/v1`。

### 3.1 Auth（16 → 16）

| 旧                              | 新                        | 变化                                         |
|---------------------------------|---------------------------|----------------------------------------------|
| `POST /auth/login`              | `POST /api/v1/auth/login` | 响应补 `id`、`refresh_token`                 |
| `POST /auth/refresh`            | 同                        | —                                            |
| `GET /auth/verify`              | 同                        | 返回 `{valid, id, username, roles}`          |
| `GET /auth/users`               | 同                        | `size ≤ 200`                                 |
| `GET /auth/users/{id}`          | 同                        | —                                            |
| `POST /auth/users`              | 同                        | 密码改 argon2                                |
| `PUT /auth/users/{id}`          | 同                        | —                                            |
| `DELETE /auth/users/{id}`       | 同                        | `admin` 保护用户（`protected=true`）拒绝删除 |
| `PUT /auth/users/{id}/password` | 同                        | —                                            |
| `PUT /auth/users/{id}/enabled`  | 同                        | —                                            |
| `PUT /auth/users/{id}/roles`    | 同                        | 角色不存在 → `1003`                          |
| `GET /auth/roles`               | 同                        | —                                            |
| `POST /auth/roles`              | 同                        | 名称重复 → `409`                             |
| `PUT /auth/roles/{id}`          | 同                        | —                                            |
| `DELETE /auth/roles/{id}`       | 同                        | `admin` 角色禁止删除                         |
| （新增）                        | `GET /api/v1/auth/me`     | 当前用户上下文                               |

### 3.2 静态字典（旧 4 个 Controller 共 60+ → 约 14 个模式化路由）

**旧版**：`dict/categories`、`dict/brands`、`dict/ports`、`dict/protocols`（各 6 操作 = 24）+ `equipment/*`（约 12）+
`base-devices/*`（约 12）+ `process-routes/*`、`steps`（约 14）。

**新版**：静态资源统一走**一个工厂 + 一张资源表**，路由形如：

```
/api/v1/static/{resource}                 GET   list（分页）
/api/v1/static/{resource}/all             GET   全量（下拉用，禁分页）
/api/v1/static/{resource}/{id}            GET   detail
/api/v1/static/{resource}                 POST  create
/api/v1/static/{resource}/{id}            PUT   update
/api/v1/static/{resource}/{id}            DELETE remove
```

**`{resource}` 白名单（10 项，封闭枚举，禁止通配）**

| resource              | 对应表                         |
|-----------------------|--------------------------------|
| `device-categories`   | `base_device_category_dict`    |
| `device-brands`       | `base_device_brand_model_dict` |
| `device-ports`        | `base_device_port_dict`        |
| `device-protocols`    | `base_device_protocol_dict`    |
| `device-data-items`   | `base_device_data_item_dict`   |
| `devices`             | `base_device_base_info`        |
| `equipments`          | `base_equipment_base_info`     |
| `equipment-functions` | `base_equipment_function_dict` |
| `process-routes`      | `base_process_route_resource`  |
| `process-steps`       | `base_process_route_step`      |

> **安全红线**：`{resource}` **必须**从 `Literal[...]` 或注册表 dict 中解析，绝不能用 `getattr(models, resource)`
> 这类动态访问 —— 否则等于把 ORM 全暴露成 CRUD 接口。
>
> **`process-steps` 特例**：其主键是 `(route_code, step_code)` 复合键，detail/update/delete 的路径需改为
> `?route_code=X&step_code=Y` 或 `/process-steps/{route_code}/{step_code}`，**不走 `{id}`**。

**`/all` 接口的缓存策略（新增，性能关键）**

```
GET /api/v1/static/{resource}/all
  → Redis key: static:{resource}:all:{version}
  → 命中直接返回（< 2ms）
  → 未命中查 DB 并回填，TTL 300s
  → 该资源任一写操作 → 主动 DEL 该 key（写后失效）
```

> 旧版前端每个页面加载都打一遍 4 个字典 `/all`（`StaticDictStore` 虽做了内存缓存，但刷新页面就丢），在 20 人并发下就是 80 次
> DB 全表扫描。这是**必须做**的优化。

**装备/路线/工步的旧接口保留情况**

| 旧接口                                              | 新位置                                       | 说明                                                   |
|-----------------------------------------------------|----------------------------------------------|--------------------------------------------------------|
| `POST /equipments` 等 CRUD                          | `/static/equipments`                         | —                                                      |
| `GET /equipments/{code}/functions`                  | `/static/equipment-functions?equipmentCode=` | 改为 query 过滤                                        |
| `POST /config/devices`（写入 `using_devices`）      | **废弃**                                     | 运行态快照表不允许手工写                               |
| `POST /config/points`（写 `using_points` 报警阈值） | **废弃**                                     | 阈值改在数据项字典/采集项配置中表达（见 02 号文档 S5） |
| `DELETE /config/points/{id}`                        | **废弃**                                     | 同上                                                   |
| `GET /process-routes/{code}/steps`                  | `/static/process-steps?routeCode=`           | —                                                      |

> **`POST /api/config/devices` 与 `POST /api/config/points` 的废弃是本次重构最重要的行为变化**
> 。旧版靠这两个接口"手工补写"运行态表，本质是在给断链的触发器打补丁。新版运行态表由 plan 驱动的 `collector_orchestrator`
> 独占写入，**任何手工写都会在下一次 `apply_plan` 后被覆盖**，因此必须从接口层禁止，并在前端运行配置页移除对应按钮。

### 3.3 运行动态配置链（旧 ~20 → 新 ~22）

| 旧                                                | 新                                                          | 变化                                                                        |
|---------------------------------------------------|-------------------------------------------------------------|-----------------------------------------------------------------------------|
| `GET /running/configs`                            | `GET /api/v1/config/equipments`                             | 改名（`configs` → `equipments` 语义更清晰）                                 |
| `POST /running/configs`                           | `POST /api/v1/config/equipments`                            | —                                                                           |
| `GET /running/configs/{id}`                       | `GET /api/v1/config/equipments/{id}`                        | 含五级链概览                                                                |
| `PUT /running/configs/{id}`                       | `PUT /api/v1/config/equipments/{id}`                        | **拒绝 `run_status` 入参**                                                  |
| `PUT /running/configs/{id}`（仅改 config_status） | `PUT /api/v1/config/equipments/{id}/status`                 | 拆出独立端点：`{config_status: 0/1/2}`                                      |
| `DELETE /running/configs/{id}`                    | `DELETE /api/v1/config/equipments/{id}`                     | 级联删除子表 + 触发重新编译                                                 |
| `GET /running/configs/{id}/expand`                | `GET /api/v1/config/equipments/{id}/expand`                 | **保留**，前端配置向导核心接口                                              |
| `POST /running/configs/{id}/sync`                 | `POST /api/v1/config/equipments/{id}/resync`                | **语义变更**：从"同步到 using 表"改为"重新编译并下发 plan"                  |
| `POST /running/configs/{id}/cleanup`              | `POST /api/v1/config/equipments/{id}/unload`                | **语义变更**：从"清理 using 表"改为"重编 plan 后从扩展卸载该 config 的设备" |
| （无）                                            | `GET /api/v1/config/equipments/{id}/plan`                   | **新增**：预览编译产物（不下发，排障必用）                                  |
| （无）                                            | `GET /api/v1/config/equipments/{id}/diff`                   | **新增**：与当前生效 revision 的差异                                        |
| 路线绑定 CRUD                                     | `POST/GET/PUT/DELETE /api/v1/config/equipments/{id}/routes` | 收进 equipments 子资源                                                      |
| 功能绑定 CRUD                                     | `/api/v1/config/equipments/{id}/functions`                  | 同上                                                                        |
| 设备绑定 CRUD                                     | `/api/v1/config/routes/{routeConfigId}/devices`             | 独立子资源                                                                  |
| 采集项 CRUD                                       | `/api/v1/config/devices/{bindingId}/collections`            | 同上                                                                        |
| （无）                                            | `POST /api/v1/config/recompile`                             | **新增**：全量重编译（运维兜底）                                            |
| （无）                                            | `GET /api/v1/config/revisions`                              | **新增**：计划版本历史                                                      |

**`/expand` 响应契约（前端配置向导硬依赖，全蛇形命名，与 02 号数据字典逐字一致）**

```json
{
  "equipment": { "config_id": 1, "config_code": "CFG-001", "equipment_code": "EQ-001",
                 "equipment_name": "一号加工中心", "config_status": 1, "run_status": 1 },
  "routes": [
    { "id": 11, "route_code": "R001", "route_name": "主装配线", "route_seq": 10, "enabled": 1,
      "devices": [
        { "id": 21, "step_code": "S010", "step_name": "上料", "step_seq": 10,
          "device_code": "DEV-001", "device_name": "PLC-1",
          "protocol_code": "MODBUS_TCP", "port_code": "ETHERNET", "enabled": 1,
          "collections": [
            { "id": 31, "item_code": "IT-TEMP", "item_name": "温度",
              "address": "40001", "data_type": "float", "collection_cycle_ms": 1000,
              "scale": 0.1, "offset": 0.0, "alarm_high": 80.0, "alarm_low": 0.0, "alarm_enabled": true,
              "enabled": 1 }
          ] }
      ] }
  ],
  "functions": [ { "id": 41, "function_code": "F-ALARM", "function_name": "报警", "function_type": "报警", "enabled": 1 } ],
  "summary": { "route_count": 1, "device_count": 1, "point_count": 1, "plan_revision": 7 }
}
```

> **必须一次性返回全链**（4 层嵌套）。旧版前端 `ConfigWizard` / `DeviceTree` 依赖这个结构渲染五级树；若拆成 4 个接口会让前端做
> 4 次串行请求并自行拼装，失去原子性。
>
> **字段名不再沿用旧驼峰**：全项目统一蛇形（02 号数据字典 → plan_compiler → `apply_plan` → WS 载荷一条线贯通）。前端
> `ConfigWizard` 需要同步改字段引用，这是本次重构唯一需要改前端字段名的响应体（其它响应体保持旧名）。

**激活流程（前端"下发"按钮的真实链路）**

```
PUT  /config/equipments/{id}/status  {config_status: 1}
  → audit(activate)
  → plan_compiler.compile(id)          # 4.6 的 SQL
  → plan_hash 与上一版比对，相同则跳（code=0, message="no change"）
  → collector.apply_plan(plan, revision)
      ├ rc=0    → 写 runtime_plan_revision(status=1) → orchestrator 刷新快照 → 返回 revision
      └ rc≠0    → 写 status=2 + extension_error → HTTP 200 + code=4000+rc
```

### 3.4 运行态查询（旧 DeviceController 读侧 ~8 → 新 ~8）

| 旧                                     | 新                                                  | 说明                                                         |
|----------------------------------------|-----------------------------------------------------|--------------------------------------------------------------|
| `GET /devices`                         | `GET /api/v1/runtime/devices`                       | 数据源 `using_devices`                                       |
| `GET /devices/{id}`                    | `GET /api/v1/runtime/devices/{device_key}`          | **主键语义改为 `device_key`**（与报警、plan 一致）           |
| `GET /devices/{id}/points`             | `GET /api/v1/runtime/devices/{device_key}/points`   | —                                                            |
| `GET /devices/{id}/commands`           | `GET /api/v1/runtime/devices/{device_key}/commands` | 最近 200 条                                                  |
| （无）                                 | `GET /api/v1/runtime/devices/{device_key}/snapshot` | **新增**：运行态快照直读（`using_devices` + `using_points`） |
| （无）                                 | `GET /api/v1/runtime/overview`                      | **新增**：总览（在线数/点位数/活动报警数/今日数据量）        |
| `GET /devices/{id}/points`（含实时值） | 同上 + WS `data` 主题                               | **实时值不走 REST**（见第五章）                              |

> **`{device_key}` 替代 `{id}` 的连带影响**：前端 `DeviceDetailView` 的路由参数、`AlarmListView` 的跳转、`ControlDialog`
> 的发起参数都要改。这是一次**前后端联动改造**，需在同一 PR 内完成，否则会出现 404 风暴。旧 `{id}` 可保留为兼容参数（内部反查
> `device_key`），但**不建议**长期保留。
>
> **`device_key` 是配置链路的主键语义**：`running_route_device` / `running_device_collection` / plan 的
> `devices[].device_key` 全部用它，REST 路径参数跟随后端，不再混用 `id`。`device_code` 是业务编码，两套并存但**不混用**（旧接口的
> `device_code` 查询参数统一改为 `device_key` 传参）。

### 3.5 历史数据（1 → 2）

| 旧             | 新                                | 变化                                                            |
|----------------|-----------------------------------|-----------------------------------------------------------------|
| `GET /history` | `GET /api/v1/data/history`        | 保持参数 `point_id/start/end/agg`                               |
| （无）         | `POST /api/v1/data/history/batch` | **新增**：多点位同时查（对比多台设备趋势，前端 TrendView 急需） |

**`agg` 合法值强化**：旧文档写 `raw/1min/1h`，实际 SQL 支持 `1d`。新版用 `Literal["raw","1min","1h","1d"]`
显式声明，非法值 → `422`，不再"静默按 raw 处理"。

**性能约束（新增）**

| 约束                 | 值         | 说明                                                                          |
|----------------------|------------|-------------------------------------------------------------------------------|
| raw 单次返回行数上限 | 50000      | 保持旧版 `LIMIT 50000`，超出时响应头带 `X-Truncated: true`                    |
| raw 时间窗上限       | **7 天**   | 超过 → `code=400`，提示改用聚合（旧版无限制，用户可查 1 年 raw 直接打爆内存） |
| 聚合查询             | 无行数限制 | 走连续聚合视图                                                                |

**`raw` 响应字段保持旧版**（前端 ECharts 已适配）：

```json
{ "rows": [ { "timestamp": 1729713600, "value": 45.23, "quality": "Good" } ], "agg": "raw", "point_id": 101 }
```

### 3.6 报警（3 → 5）

| 旧                         | 新                                | 变化                                                         |
|----------------------------|-----------------------------------|--------------------------------------------------------------|
| `GET /alarms`              | `GET /api/v1/alarms/list`         | 新增 `state`、`level`、`device_key` 过滤（旧版只有 keyword） |
| `PUT /alarms/{id}/ack`     | `PUT /api/v1/alarms/{id}/ack`     | 幂等：已 ack 再 ack 返回 `code=0`                            |
| `PUT /alarms/{id}/recover` | `PUT /api/v1/alarms/{id}/recover` | —                                                            |
| （无）                     | `GET /api/v1/alarms/stats`        | **新增**：按等级/设备/时段统计（前端报警大盘）               |
| （无）                     | `PUT /api/v1/alarms/batch/ack`    | **新增**：批量确认（旧版需逐条点，操作员痛点）               |

**报警状态机（与 02 号文档 5.4 一致）**

```
触发(1 活动) ──ack──► 已确认(2) ──value恢复正常──► 已恢复(3)
     │                                          ▲
     └───────────── value 恢复正常 ───────────────┘
```

> **旧版缺陷**：`alarm_acknowledged` 事件只带 `alarmId`，前端需回列表里按 id 改 `state`（02.02 文档 7.2 的 `alarm`
> 回调里明确写了这个妥协）。新版 WS 报警事件**直接携带完整报警对象**（见第五章），前端拿到即可直接更新，无需二次查表。

### 3.7 控制（1 → 4）

| 旧                           | 新                                                   | 变化                                                                |
|------------------------------|------------------------------------------------------|---------------------------------------------------------------------|
| `POST /devices/{id}/control` | `POST /api/v1/control/{device_code}/write`           | **改为异步**：入队即返回 `command_id`                               |
| 同上（同步语义）             | `POST /api/v1/control/{device_code}/write?wait=true` | 最长等 5s，超时 → `code=4102`                                       |
| （无）                       | `GET /api/v1/control/{command_id}`                   | **新增**：查单条命令状态                                            |
| （无）                       | `GET /api/v1/control/queue`                          | **新增**：查看扩展控制队列深度（排障用，取 `health().queue_depth`） |

**`write` 请求体（全蛇形）**

```json
{ "point_id": 101, "point_name": "启动信号", "value": "1" }
```

**响应**

```json
{ "command_id": 42, "accepted": true, "message": "queued" }
```

> **`command_id` 直接取自 `collector.write_point(device_key, point_id, value)` 的返回值**（05 号 5.2），扩展是唯一的发号方，FastAPI
> 不再自己生成，避免"HTTP 给的号"与"执行时的号"对不上。

**关键校验（新增，旧版缺失）**

| 校验                                            | 失败码   | 说明                                             |
|-------------------------------------------------|----------|--------------------------------------------------|
| 点位存在                                        | 404      | —                                                |
| `read_write ∈ {W, RW}`                          | **4101** | 旧版对只读点位也允许下发，驱动层再报错           |
| 设备在线（`using_devices.status=1` 运行态快照） | **4103** | 旧版直接尝试连接，等待超时                       |
| 值类型可解析（按 `data_type`）                  | 400      | `data_type=int` 传 `"abc"` 直接拒                |
| 该设备 `function_type='控制'` 已启用            | **4104** | 见 02 号文档 4.3（控制类业务码，不占用扩展段位） |

**状态回执方式**

```
扩展事件 write_result → ingest worker → UPDATE control_commands.status
                                     → WS 广播 control_finished
```

> **旧版把"连接 → 写 → 断开"三段全塞在 HTTP 请求里**，单次控制耗时 50~500ms 且期间占用 worker 线程。新版 `write` 只需 1 次
> `collector.write_point()` 调用（扩展队列 + 驱动线程池执行），p99 < 30ms（主文档 11.1 已定指标）。

### 3.8 采集扩展探针（新增 5）

| 新端点                            | 用途                                                                                                                  | 权限  |
|-----------------------------------|-----------------------------------------------------------------------------------------------------------------------|-------|
| `GET /api/v1/collector/protocols` | 扩展实际支持的协议清单（`list_protocols()`），附 `protocol_capabilities()` 的能力位（是否可写/最大点数）              | admin |
| `GET /api/v1/collector/health`    | `{initialized, revision, device_count, point_count, queue_depth, threads, dropped, stats}`，来自 `Collector.health()` | admin |
| `GET /api/v1/collector/stats`     | 采集吞吐（点/秒）、失败率、各协议建链数（`health()` 的 `stats` 段的只读投影）                                         | admin |
| `POST /api/v1/collector/replay`   | 手动重放指定 `revision` 的 plan（崩溃恢复兜底），内部即 `collector.apply_plan(plan, revision)`                        | admin |
| `POST /api/v1/collector/reload`   | 重建 `Collector` 实例并重放当前 plan（进程内重建，**不是**重载 `.so`，扩展模块一经 import 不可卸载）                  | admin |

> **`/api/v1/collector/*` 是 FastAPI 版特有的可观测性入口**。旧版 C++ 进程是黑盒，日志散在文件中、无运行态查询，只能靠
> `ps` + 日志 grep。扩展形态下**必须**有这套端点，否则"扩展线程卡死但进程活着"这类问题无法定位。
>
> **前缀用 `collector` 而不用 `kernel`**：本目录全部文档已把"内核"统一改称"采集扩展"，URL
> 也必须同步改名，否则前端联调时"后端叫 collector、URL 叫 kernel"又会制造新的口径分裂。
>
> **端点只有 5 个，没有 `remove_plan` / `validate_plan` 对应的探针**：计划撤销走"重编 + `apply_plan` 新 revision"（见 01 号
> 7.5），plan 预览校验在 `plan_compiler`（Python 侧，不跨扩展边界）。
>
> **`/collector/reload` 的真实语义要说清**：pybind11 扩展模块被 `import` 后**无法卸载热替换**（这是 Python C 扩展的固有限制，见
> 05 号 7.1），因此该端点做的是**销毁旧 `Collector` 对象、按当前 plan 重建一个**，用于"扩展内部状态脏了但不想重启进程"的场景。真正的
> C++ 代码更新必须 `systemctl restart ibmcp-api`。
>
> **`/collector/dump` 已取消**：完整运行态导出会把 `DataStore` 快照与全部 plan 打进 HTTP 响应，生产上极易打爆内存；排障改用
> `GET /collector/health` + `journalctl -u ibmcp-api` 的组合。

---

## 四、前端四大模块与接口的对应关系（02.02 号文档重写）

### 4.1 模块总览

| 模块                   | 页面数 | 依赖接口组                                         | 依赖 WS 主题                         |
|------------------------|--------|----------------------------------------------------|--------------------------------------|
| 模块一：系统管理       | 3      | `/auth/*`                                          | 无                                   |
| 模块二：基础数据管理   | 6      | `/static/*`                                        | 无                                   |
| 模块三：运行时配置管理 | 5      | `/config/*`                                        | `device`（状态回显）                 |
| 模块四：控制与监控管理 | 5      | `/runtime/*`、`/data/*`、`/alarms/*`、`/control/*` | `data`、`device`、`alarm`、`control` |

### 4.2 模块一：系统管理（`/auth`）

| 功能点     | 前端           | 新接口               | 关键变化                                       |
|------------|----------------|----------------------|------------------------------------------------|
| 登录       | `LoginView`    | `POST /auth/login`   | 响应补 `id`，前端 `authStore.userInfo.id` 可用 |
| Token 刷新 | request 拦截器 | `POST /auth/refresh` | 过期前 30s 静默刷新（保持）                    |
| 路由守卫   | router guard   | `GET /auth/verify`   | 401 时清 store 跳登录                          |
| 用户管理   | `UserManage`   | `/auth/users` 全套   | 密码强度校验移到前端 + 后端双验                |
| 角色管理   | `RoleManage`   | `/auth/roles` 全套   | `admin` 角色禁删                               |

**角色 → 模块可见性（401/403 双保险）**

| role       | 系统管理 | 基础数据 | 运行配置 | 控制监控          |
|------------|----------|----------|----------|-------------------|
| `admin`    | 读写     | 读写     | 读写     | 读写              |
| `engineer` | 只读     | 读写     | 读写     | 读写              |
| `operator` | 不可见   | 只读     | 只读     | 只读 + ack + 控制 |
| `viewer`   | 不可见   | 只读     | 只读     | 只读              |

> **旧版只有前端守卫**（隐藏菜单），后端不拦。攻击者直接 curl `POST /static/devices` 就能改配置。新版 `require_roles`
> 依赖在路由声明时写死，**前端菜单隐藏只是体验优化，不是安全边界**。

### 4.3 模块二：基础数据管理（`/static`）

| 功能点     | 页面                    | 新接口                        | 关键变化                                         |
|------------|-------------------------|-------------------------------|--------------------------------------------------|
| 设备分类   | `DeviceCategoryView`    | `/static/device-categories`   | 走工厂六件套                                     |
| 品牌型号   | `DeviceBrandView`       | `/static/device-brands`       | 唯一键改组合                                     |
| 端口字典   | `DevicePortView`        | `/static/device-ports`        | —                                                |
| 协议字典   | `DeviceProtocolView`    | `/static/device-protocols`    | **新增 `has_driver` 字段，无驱动项置灰**         |
| 数据项字典 | `DataItemView`          | `/static/device-data-items`   | **新增 scale/offset/alarm 5 字段，表单必须补齐** |
| 基础设备   | `DeviceBaseView`        | `/static/devices`             | `supported_protocols` 用扩展清单校验             |
| 装备主数据 | `EquipmentView`         | `/static/equipments`          | —                                                |
| 装备功能   | `EquipmentFunctionView` | `/static/equipment-functions` | —                                                |
| 工艺路线   | `ProcessRouteView`      | `/static/process-routes`      | 级联工步（保持）                                 |
| 工步       | 路线详情内              | `/static/process-steps`       | 复合键路径                                       |

**前端必须同步改的 3 处（否则配置完采不到）**

1. **数据项字典表单**：新增 `scale` / `offset` / `alarm_high` / `alarm_low` / `alarm_enabled` 五个输入项；`scale` 加前端校验
   `!= 0`（02 号文档 3.6 明确该值为 0 会让读数恒定）。
2. **协议下拉**：数据源从 `GET /static/device-protocols/all` 改为 `GET /api/v1/collector/protocols`（或
   `/static/device-protocols/all?with_driver=true`），**只允许选扩展真支持的**。
3. **地址字段**：数据项表单把 `address_rule` 从"可选"改为**必填**（纯控制项除外），标签明确写"通信地址（非编码）"。

**`StaticDictStore` 缓存策略（沿用 + 强化）**

```ts
// 旧：内存缓存，刷新丢失
// 新：内存 + localStorage + 版本号
const KEY = 'ibmcp_dict_v1'
state: { version: 1, cache: Record<string, any[]>, loadedAt: {} }
```

> 配合后端 `/all` 的 Redis 缓存，前端页面切换时字典加载从"数百毫秒 DB 扫描"降到"毫秒级读取"。

### 4.4 模块三：运行时配置管理（`/config`）

| 功能点            | 页面                  | 新接口                                    | 关键变化                     |
|-------------------|-----------------------|-------------------------------------------|------------------------------|
| 装备运行配置 CRUD | `EquipmentConfigView` | `/config/equipments`                      | —                            |
| 配置状态切换      | 同上（Switch）        | `PUT /config/equipments/{id}/status`      | **独立端点**，激活即触发下发 |
| 配置向导          | `ConfigWizard`        | `/config/equipments/{id}/expand`          | 保持 4 层嵌套                |
| 路线绑定          | 向导内 Step2          | `/config/equipments/{id}/routes`          | —                            |
| 功能绑定          | 向导内 Step3          | `/config/equipments/{id}/functions`       | —                            |
| 设备绑定          | 向导内 Step4          | `/config/routes/{routeConfigId}/devices`  | —                            |
| 采集项配置        | 向导内 Step5          | `/config/devices/{bindingId}/collections` | **新增 5 个阈值覆盖字段**    |
| 计划预览          | **新增按钮**          | `GET /config/equipments/{id}/plan`        | 下发前必看                   |
| 重新编译          | **新增按钮**          | `POST /config/equipments/{id}/resync`     | 替代旧"同步到 using 表"      |
| 卸载              | 停用时自动            | `POST /config/equipments/{id}/unload`     | 替代旧"清理 using 表"        |

**配置向导 5 步流程（保持旧版 UX，只改回调语义）**

```
Step1 选装备模板 → GET /static/equipments/all
Step2 绑路线     → POST /config/equipments/{id}/routes
Step3 绑功能     → POST /config/equipments/{id}/functions
Step4 绑设备     → POST /config/routes/{rid}/devices（须选 (step_code, device_key)）
Step5 配采集项   → POST /config/devices/{device_key}/collections（须填 address）
完成 → PUT /status {config_status:1} → 自动编译下发 → 显示 revision 号
```

**旧版"一键级联展开"接口的保留**：`GET /config/equipments/{id}/expand` 保持不变，是向导渲染五级树的**唯一数据源**（02.02 文档
6.4）。

**移除的操作（必须同步删前端按钮）**

| 旧按钮               | 所在页       | 移除原因                                  |
|----------------------|--------------|-------------------------------------------|
| "同步到运行表"       | 装备配置列表 | 改名为"重新编译下发"，语义不同            |
| "清理运行表"         | 装备配置列表 | 改为自动（停用即卸载），无需手工          |
| "手工添加运行设备"   | 设备管理     | `using_devices` 禁止手工写（3.2）         |
| "设置采集点报警阈值" | 点位配置     | 阈值改在数据项字典 / 采集项配置中配（S5） |

### 4.5 模块四：控制与监控管理（`/runtime` `/data` `/alarm` `/control`）

| 功能点       | 页面               | 新接口                                        | 关键变化                              |
|--------------|--------------------|-----------------------------------------------|---------------------------------------|
| 实时数据看板 | `DashboardView`    | `/runtime/devices/{code}/points` + WS `data`  | 点位列表带静态信息，**实时值只走 WS** |
| 历史趋势     | `TrendView`        | `/data/history`                               | 新增点位数与时间窗约束                |
| 多设备对比   | **新增**           | `/data/history/batch`                         | —                                     |
| 设备列表     | `DeviceListView`   | `/runtime/devices`                            | 主键 `device_key`                     |
| 设备详情     | `DeviceDetailView` | `/runtime/devices/{device_key}` + `/snapshot` | **新增运行态快照面板**                |
| 控制下发     | `ControlDialog`    | `/control/{device_key}/write`                 | 异步 + 回执走 WS                      |
| 命令历史     | 设备详情内 Tab     | `/runtime/devices/{device_key}/commands`      | 保持 200 条                           |
| 报警列表     | `AlarmListView`    | `/alarms/list`                                | 新增过滤维度                          |
| 报警横幅     | `AlarmBanner`      | WS `alarm`                                    | payload 携带完整对象                  |
| 报警大盘     | **新增**           | `/alarms/stats`                               | —                                     |

**`DashboardView` 的数据流（旧版 4 步，新版 4 步，第 2 步变了）**

```
1. GET /runtime/devices/{device_key}/points    → 点位静态信息（名称/单位/量程/质量码枚举）
2. WS subscribe('data')                        → 全局数据流，前端按 point_id 命中
   （旧版同样订阅 'data'，topic 固定字符串、无精确订阅 → 保持不变）
3. 实时值渲染 RealTimePoint                     → wsStore.realtimeData[point_id]
4. 折线图滑动窗口 100 点                         → 保持
```

> **不要试图改成"按点位订阅"**：旧版注释已明确 `topic` 是固定字符串、不能带 `:id`（02.02 文档 7.2 与 9.11）。新版**保持全局
> `data` 主题**，前端过滤。若未来点位规模 > 5 万、单机 WS 带宽成为瓶颈，再考虑引入"客户端侧过滤表达式"（服务端按连接下发过滤规则），属
> P2。

---

## 五、WebSocket 契约（四固定主题，载荷强化）

### 5.1 连接与协议帧

```
端点：ws://<host>/api/v1/ws/collect        # 注意带 /api/v1 前缀（与 backend/app/api/v1/ws.py 一致）
首帧（客户端）：{"type":"subscribe","topic":"data"}
服务端确认：  {"type":"subscribed","topic":"data"}
保活：        {"type":"ping"} → {"type":"pong"}
退订：        {"type":"unsubscribe","topic":"data"}
```

**鉴权**：连接时 URL 带 `?token=<jwt>`（浏览器 WS 无法设 Header），`on_accept` 前校验，失败 `close(code=4001)`。

### 5.2 四主题载荷契约（含新旧差异）

#### ① `data`（采集数据）

```json
{ "type": "data", "device_code": "DEV-001", "point_id": 101,
  "value": 45.23, "quality": "Good", "ts": 1729713600, "error": null }
```

| 字段                       | 旧版 | 新版                  | 差异                            |
|----------------------------|------|-----------------------|---------------------------------|
| `device_id`                | int  | **`device_code`** str | 与报警/控制/plan 对齐，改字段名 |
| `point_id`                 | int  | int                   | 保持（`using_points.id`）       |
| `value` / `quality` / `ts` | 有   | 有                    | 保持                            |
| `error`                    | 有   | 有                    | 保持                            |

> **扩展侧 → WS 的唯一换算层在 Python**：扩展输出 `device_key` + `ts_ms`（毫秒），`EventDispatcher`/`SamplePump` 统一改名为
> WS 契约的 `device_code` + `ts`（秒）。**这两个改名只在这一处发生**，扩展与 WS 各自的边界内都不做驼峰映射。
>
> **全蛇形是这条链路的硬约束**：扩展事件 → `EventDispatcher` → WS 广播全程无驼峰名，任何一个环节出现驼峰都会在广播层被迫做字典映射，既慢又易错。

**批量压缩（新增，吞吐关键）**：单条一个 JSON 帧在 5000 点 × 1s 周期下 = 5000 帧/秒，浏览器会堆帧。新版**按 200ms 窗口攒批**
下发：

```json
{ "type": "data_batch", "samples": [
   {"device_code":"DEV-001","point_id":101,"value":45.23,"quality":"Good","ts":1729713600},
   {"device_code":"DEV-001","point_id":102,"value":12.5,"quality":"Good","ts":1729713600}
], "window_ms": 200 }
```

> 前端兼容：`wsStore.subscribeData` 里同时处理 `data`（单条，兼容旧）与 `data_batch`（批量）。**单条模式仅用于小规模（< 200
> 点）配置**，由服务端按 `settings.ws_batch_threshold` 自动切换。

#### ② `device`（设备状态）

```json
{ "type": "device_status", "device_code": "DEV-001", "status": 1, "ts": 1729713600, "reason": "" }
```

| 字段                            | 差异                                                                                       |
|---------------------------------|--------------------------------------------------------------------------------------------|
| `device_id` → **`device_code`** | breaking（扩展侧对应字段为 `device_key`，在 Python 层换名）                                |
| `reason`                        | **新增**，离线原因（`connect_timeout` / `auth_failed` / `protocol_error`），前端可直接展示 |
| `status`                        | 0=离线 1=在线 2=异常（**新增 2**）                                                         |

> 事件类型与 05 号 6.3 的 `device_status` 事件**同一份契约**（`type` 名、字段语义全部一致）。唯一差异是时间戳与设备标识的
> **对外投影**：扩展给 `device_key` + `ts_ms`（毫秒），WS 给 `device_code` + `ts`（秒），换算在本节 ① 所说的同一处 Python 层完成。

#### ③ `alarm`（报警）

```json
{ "type": "alarm_triggered", "alarm": {
    "id": 501, "device_code": "DEV-001", "point_address": "40001", "point_name": "温度",
    "level": 3, "state": 1, "message": "温度超上限", "value": 96.5, "triggered_at": 1729713600 } }
```

```json
{ "type": "alarm_acknowledged", "alarm": { "id": 501, "state": 2, "acknowledged_at": 1729713700 } }
{ "type": "alarm_recovered",  "alarm": { "id": 501, "state": 3, "recovered_at": 1729713800, "value": 72.1 } }
```

| 字段                                     | 旧版                                                                     | 新版                                                                                                       |
|------------------------------------------|--------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------|
| `alarm_triggered` 载荷                   | 平铺 `deviceCode/pointAddress/pointName/level/message/value/triggeredAt` | **嵌套在 `alarm` 对象内**，且带 `id` 与 `state`，字段全蛇形（`device_key`/`point_address`/`triggered_at`） |
| `alarm_acknowledged` / `alarm_recovered` | **仅 `alarmId`**                                                         | **携带完整对象**（含 `state`、`acknowledged_at`/`recovered_at`）                                           |

> **修复旧版妥协**：旧文档 7.2 明确写了"仅拿到 alarmId，需按 id 回列表里改 state=2"。新版直接给完整对象，前端
> `AlarmBanner` / `AlarmListView` 可直接 upsert，不用二次查表也不用维护列表匹配逻辑。

#### ④ `control`（控制回执）

```json
{ "type": "control_finished", "command_id": 42, "device_code": "DEV-001", "point_id": 101,
  "success": true, "status": 2, "message": "ok", "value": "1", "finished_at": 1729713601 }
```

| 字段                            | 差异                                                               |
|---------------------------------|--------------------------------------------------------------------|
| `device_id` → **`device_code`** | breaking                                                           |
| `status`                        | **新增**，0/1/2/3/4，与 `control_commands.status` 一致             |
| `finished_at`                   | **新增**，用于前端计算耗时                                         |
| 失败                            | `success=false` 时 `status=3` 或 `4`，`message` 带扩展错误码与文案 |

> **`control_finished` 直接由扩展的 `write_result` 事件投影而来**（05 号 6.3），字段映射：`command_id ← command_id`、
> `device_code ← device_key`、`finished_at ← ts_ms/1000`，其余同名直通。因此扩展侧 `write_result` 必须输出蛇形 `command_id` /
> **`device_key`** / `point_id`，WS 层在此处把 `device_key` 改名为 `device_code`（与 5.2① 的统一换算层一致）。

### 5.3 服务端推送实现（与主文档 8.x 一致）

```
扩展线程 ──回调──► event_queue（有界队列，仅低频事件）
                        │
                        └─► ws_broadcast（独立 asyncio 任务）
                              └─► 按 4 主题分组、200ms 攒批、单连接限速

SamplePump（200ms）──snapshot_since()──► samples ──► 批写 COPY（DB）
                                                  └─► 攒批后推 data 主题
```

**关键点**：

1. **采样不进回调队列**：`data` 主题的数据源是 `SamplePump` 拉取的快照（200ms 一次），不走 C++ 回调 —— 5000 点 × 1s
   若每点一次回调，GIL 切换本身就会吃掉全部预算（05 号 3.3）。
2. **广播不等待落库**：攒批后的 `data` 与 `alarm` 先广播再落库（主文档 11.1 的 `p99<500ms` 指标），落库失败只记日志不阻断
   WS。
3. **慢客户端隔离**：单连接发送队列 > 500 帧即丢弃**最旧**的 `data` 帧（`device` / `alarm` / `control`
   不丢），避免一个卡顿浏览器拖垮全部连接。
4. **单 worker 承载 WS**：`uvicorn --workers 1`，扩展的事件只落在持有扩展的进程；多 worker 会让 WS
   连接分裂到收不到事件的进程。吞吐靠异步与批量，不靠多进程（见主文档 9.6）。

### 5.4 前端 `IbmcpWsClient` 改动清单

| 位置                           | 改动                                                                                         |
|--------------------------------|----------------------------------------------------------------------------------------------|
| `comm/ws.ts` 的 `subscribe`    | 保持 API 不变（`topic` 仍是 4 个固定字符串）                                                 |
| `stores/ws.ts` `subscribeData` | 新增 `data_batch` 分支；`payload.device_id` → `device_code`，`pointId` → `point_id` 同步改名 |
| `stores/ws.ts` 设备状态回调    | 字段重命名 + 处理 `status=2`（异常态样式）                                                   |
| `stores/ws.ts` 报警回调        | 改为读 `payload.alarm` 完整对象直接 upsert（字段全蛇形）                                     |
| `stores/ws.ts` 控制回调        | 新增 `status` / `finished_at` 展示（耗时）                                                   |
| 连接 URL                       | `?token=` 追加 JWT                                                                           |
| 重连                           | 指数退避保持；新增 401/4001 时不重连、直接跳登录                                             |

---

## 六、测试用例重写要点（03 号文档的用例需按新契约改写）

### 6.1 必须新增的用例组

| 组               | 用例                                                                                                        | 目的                     |
|------------------|-------------------------------------------------------------------------------------------------------------|--------------------------|
| T1 配置下发闭环  | 建装备配置 → 绑路线/设备/采集项 → 激活 → 查 revision → 查 `/plan` → 验证 plan 中 `address/scale/alarm` 正确 | 验证 S5/S6/S7 修复       |
| T2 编译失败路径  | 采集项缺 `address` → 激活 → 期望 `code=3002` 且 message 指明 item                                           | 验证不再静默用 item_code |
| T3 协议不支持    | 字典写 `PROFINET` → 绑设备 → 激活 → 期望 `code=4001`                                                        | 验证 has_driver          |
| T4 计划幂等      | 连续两次激活，配置未变 → 第二次 `message="no change"`、revision 不变                                        | 验证 plan_hash           |
| T5 崩溃恢复      | 杀进程 → 重启 → 自动重放 `status=1` revision                                                                | 验证主文档 7.7           |
| T6 只读点位控制  | 对 `read_write=R` 点位 write → `code=4101`                                                                  | 验证新增校验             |
| T7 快照一致性    | plan_ack 后查 `using_devices` / `/snapshot` 与 plan 一致                                                    | 验证 S2                  |
| T8 RBAC          | `viewer` token 调 `POST /static/devices` → HTTP 403                                                         | 验证强制鉴权             |
| T9 分页上限      | `size=1000` → `422`                                                                                         | 验证新约束               |
| T10 报警重复触发 | 两 worker 并发判定同一点 → 只有 1 条 `state=1`                                                              | 验证 5.4 的唯一索引      |
| T11 WS 批量帧    | 200+ 点配置 → 收到 `data_batch` 而非逐条                                                                    | 验证攒批                 |
| T12 慢客户端     | 模拟无消费连接 → 该连接被丢 `data` 帧，其他连接正常                                                         | 验证隔离                 |

### 6.2 必须废弃的旧用例

| 旧用例                                                    | 废弃原因                           |
|-----------------------------------------------------------|------------------------------------|
| `POST /api/config/devices` 建运行设备                     | 接口已废弃（3.2）                  |
| `POST /api/config/points` 设报警阈值                      | 接口已废弃，阈值改配在字典/采集项  |
| `POST /api/running/configs/{id}/sync` 后查 `using_points` | 语义已变，改为查 plan / revision   |
| 同步后用 `item_code` 当地址断言                           | 正是 S6 要修的缺陷，不能作为期望值 |

### 6.3 测试数据依赖顺序（保持旧版链条，去掉手工补写环节）

```
① 静态字典（category/brand/port/protocol/data-item）
② 基础设备 + 装备主数据 + 装备功能
③ 工艺路线 + 工步（含 device_code 绑定）
④ 运行装备配置（config_status=0）
⑤ 路线绑定 → 设备绑定 → 采集项配置（address_override 可空，依赖 address_rule）
⑥ 激活（config_status=1）→ 自动 plan → 自动快照
⑦ 等待数据 → 查历史 → 触发报警 → ack/recover
⑧ 控制下发 → 查回执 → 查命令历史
```

> **⑤ 之后不再有"手工补写 using_points 字段"这步**。旧版必须先 `POST /config/points` 补 scale/alarm 才能测报警 ——
> 这条链在新版被彻底删除，这也是 T1 用例要专门验证的点。

---

## 七、开发落地顺序（建议）

| 阶段 | 内容                                                                                          | 依赖               |
|------|-----------------------------------------------------------------------------------------------|--------------------|
| P0   | `ResponseEnvelopeMiddleware`、`current_user`、`StaticDictRouter`、`kernel/ffi.py` + `stub.py` | 主文档 4/7 章      |
| P1   | `/auth/*` 全量 + `/static/*` 十资源 + Redis `/all` 缓存                                       | P0                 |
| P2   | `/config/*` 五级链 + `plan_compiler` + `runtime_plan_revision` + `/plan` 预览                 | P0 + 02 号文档 4.6 |
| P3   | `/collector/*` 探针 + `apply_plan` 真实联调（欧拉侧）                                         | C++ 库可用         |
| P4   | `/runtime/*`、`/data/*`、`/alarms/*`、`/control/*`                                            | P2/P3              |
| P5   | WS `data_batch` 攒批 + 慢客户端隔离 + 报警事件载荷升级                                        | P4                 |
| P6   | 前端四大模块联动改造（字段重命名 + 移除废弃按钮）                                             | P1~P5              |

> **P6 必须与 P2/P4 同 PR 交付的部分**：`device_id → device_key` 重命名（WS 与 REST 全链路）、`/config/points` 与
> `/config/devices` 废弃后的按钮移除。这三处**不能分批上线**，否则中间态会出现前端调 404。

---

## 八、与旧文档的对照索引

| 旧文档                 | 章节                                           | 本文档对应             |
|------------------------|------------------------------------------------|------------------------|
| `03-API接口及测试用例` | 0.1~0.8 通用约定                               | 第二章                 |
| 同上                   | 1 AuthController                               | 3.1 + 4.2              |
| 同上                   | 2 DeviceController                             | 3.4 + 3.7              |
| 同上                   | 2.7 控制命令执行                               | 3.7（异步化重写）      |
| 同上                   | 2.7 command.status 状态机                      | 3.7 + 5.2④             |
| 同上                   | 3 ConfigController                             | 3.2（**废弃**）        |
| 同上                   | 4 AlarmController + 4.4 报警规则 + 4.5 状态机  | 3.6 + 02 号文档 5.4    |
| 同上                   | 5 DataController                               | 3.5                    |
| 同上                   | 6~9 静态字典 / 基础设备 / 工艺路线             | 3.2                    |
| 同上                   | 10 运行装备配置                                | 3.3                    |
| `02.02-Vue前端`        | 4 模块一系统管理                               | 4.2                    |
| 同上                   | 5 模块二基础数据                               | 4.3                    |
| 同上                   | 6 模块三运行配置 + 6.3 配置向导 + 6.4 级联展开 | 4.4 + 3.3 `/expand`    |
| 同上                   | 7 模块四控制监控 + 7.2 四主题消费              | 4.5 + 第五章           |
| 同上                   | 7.2 `alarm` 回调的"仅 alarmId"妥协             | 5.2③（已修复）         |
| 同上                   | 9.11 topic 固定字符串、无精确订阅              | 4.5（保持该约束）      |
| 同上                   | 10 HTTP 封装 / 11 WS 底层客户端                | 2.1 + 5.4              |
| `02.01-Cpp_Drogon`     | 路由全集                                       | 3.x 映射表             |
| `02.01-Cpp_Drogon`     | 驱动工厂 `normalize()`                         | 02 号文档 3.5          |
| `04-协议完善指南`      | 7 项协议状态                                   | 02 号文档 3.5 + 第六章 |
| `01-开发架构指南`      | 2.5 数据库 / 2.6 驱动                          | 02 号文档第三~六章     |

---

## 九、待确认（与 02 号文档 D 清单合并编号）

| 编号 | 问题                                                                                                                            | 影响接口 |
|------|---------------------------------------------------------------------------------------------------------------------------------|----------|
| A1   | WS 载荷 `device_id` → `device_key`、`device_code` → 蛇形，是否接受 breaking change？如不接受需在 WS/REST 双发两字段（体积+30%） | 3.4、5.2 |
| A2   | `/api/*` 旧路径是否需要在 Nginx 层 rewrite 兼容？                                                                               | 1.3      |
| A3   | WS 是否允许为了吞吐牺牲一致性（收敛单 worker 广播）？                                                                           | 5.3      |
| A4   | `size` 上限 200 是否合适（大数据量列表页需改服务端筛选+游标分页）？                                                             | 2.4      |
| A5   | 是否保留 `/config/devices`、`/config/points` 的只读查询能力（写能力已废弃）？                                                   | 3.2      |
| A6   | `/collector/reload` 是否上线（热替换共享库风险高，可能需改为"重启进程"）？                                                      | 3.8      |