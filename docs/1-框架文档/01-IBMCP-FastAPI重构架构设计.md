# IBMCP FastAPI 重构架构设计

> 版本：v1.0　　编写日期：2026-11-18　　目标运行环境：openEuler（欧拉）　　开发环境：Windows 11
>
> 本文档描述 IBMCP **FastAPI 业务主框架 + C++ 采集扩展（pybind11 扩展模块）**
> 的目标架构，并给出目录结构、接口契约、数据流、并发模型、部署与远程开发方案、分期落地路线。
>
> 原 C++17 + Drogon 单体后端已停止维护，本文档不再保留任何 Drogon 并行灰度、回退、双跑的设计；历史方案仅作为设计决策的背景参照。
>
> 对照基线文档：
> -
> 静态配置：[01-静态数据表字段定义.md](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/01-docs/数据字典/01-静态数据表字段定义.md)
> -
运行动态配置：[02-运行动态配置表字段定义.md](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/01-docs/数据字典/02-运行动态配置表字段定义.md)
> -
运行期核心与系统表：[03-运行期核心与系统管理表字段定义.md](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/01-docs/数据字典/03-运行期核心与系统管理表字段定义.md)
> - 现有 Drogon
>   后端：[02.01- Cpp_Drongon_软件代码架构说明.md](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/01-docs/开发指南/02.01-%20Cpp_Drongon_软件代码架构说明.md)

---

## 一、重构目标与边界

### 1.1 用户提出的四条硬性要求

| 编号 | 要求                                                                                                                         | 本文档对应章节         |
|------|------------------------------------------------------------------------------------------------------------------------------|------------------------|
| R1   | 核心框架改为 FastAPI；静态字段配置管理对齐 `01-静态数据表字段定义.md`，运行期动态配置管理对齐 `02-运行动态配置表字段定义.md` | 第四章、第五章         |
| R2   | 除"基于各类协议的数据采集"用 C++ 实现外，其余全部走 FastAPI                                                                  | 第三章、第六章         |
| R3   | FastAPI 把"用哪个协议采哪些数据"下发给 C++ 子模块，并获取其反馈；C++ 需作为 FastAPI 可调用的**子模块/库**                    | 第七章（核心接口契约） |
| R4   | 保持多线程、高并发、高吞吐                                                                                                   | 第八章                 |
| R5   | 最终运行在欧拉系统（PG15 / Redis / Nginx 等）；开发在 Windows 11，调试在欧拉                                                 | 第九章、第十章         |

### 1.2 职责切分原则（一句话）

> **C++ = 协议与 IO 的"手"；FastAPI = 配置、业务、接口与治理的"脑"。**
> C++ 不认识数据库、不认识 HTTP、不认识 RBAC、不认识工艺路线；它只认"连接参数 + 点表 + 周期"，然后把读数与控制结果吐回来。

### 1.3 明确划归 FastAPI（不再出现在 C++ 中）

- 全部 REST API、WebSocket 推送、鉴权（JWT + RBAC）
- 静态字典表 CRUD（`base_*` 10 张表）
- 运行动态配置 CRUD 与级联展开（`running_*` 5 张表）
- 配置下发编排（`running_*` → 采集计划 plan）与配置生效/回滚
- 时序数据写入、报警判定、控制命令台账、历史查询
- 采集器状态聚合、运维端点、指标暴露

### 1.4 明确划归 C++ 采集扩展

- 协议栈：Modbus TCP/RTU（libmodbus）、OPC UA（open62541）、MQTT（libmosquitto）、Socket TCP/UDP、WebSocket 客户端
- 连接生命周期管理、批量读合并、寄存器编解码
- `scale/offset`
  原始值换算（保持现有 [BaseDriver.h](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/backend/include/drivers/BaseDriver.h)
  语义）
- 时间轮驱动的采集调度 + 固定大小 IO 线程池、超时与重连、点位新鲜度
- 写入（控制）执行与结果回执

---

## 二、当前架构问题诊断（迁移动因）

基于历史方案（原 Drogon 单体后端）的复盘，实际存在的痛点如下：

| 问题                               | 证据                                                                                                                                                                                      | 影响                                                                              |
|------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------|
| 采集线程 = 设备数，线程无上限      | [CollectorService.h](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/backend/include/services/CollectorService.h) 中 `std::map<int64_t, DevicePlan> plans_`，每 plan 一个 `std::thread` | 现场 500 台设备即 500 线程，CPU 上下文切换与栈内存（默认 8MB/线程虚拟地址）压力大 |
| 时序写入逐点同步 INSERT            | [DataService::ingest](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/backend/src/services/DataService.cpp#L155-L184) 每点一次 `execSqlSync`                                            | 500 点/秒即 500 条独立 SQL，无法批量化，写入成为吞吐瓶颈                          |
| 报警判定每点一次额外查询           | [checkAndFireAlarm](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/backend/src/services/DataService.cpp#L95-L153) 内 `SELECT ... FROM using_points` + `findActiveForPoint`             | 单点写入放大为 3 次 DB 往返                                                       |
| 配置生效依赖 DB 触发器，业务不透明 | `db/01-init.sql` 中 `trg_running_config_sync` / `sync_config_to_using_tables()`                                                                                                           | 配置变更无版本、无回滚、无下发回执；C++ 只能靠 5 秒轮询发现变更                   |
| 采集与 HTTP 在同一进程             | [main.cpp](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/backend/src/main.cpp) 中 `registerBeginningAdvice` 直接起采集器                                                              | 采集内存泄漏/崩溃会连带 API 服务；无法独立扩缩容                                  |
| 配置变更延迟固定 5 秒              | `CollectorService::start(5)`，`reloadPlan()` 轮询                                                                                                                                         | 现场"改完参数要等几秒才生效"                                                      |
| 协议栈故障隔离差                   | 驱动在进程内，段错误直接杀死整个后端                                                                                                                                                      | 一个设备的第三方协议栈 bug 导致全站掉线                                           |

**这次重构正是在解决上述问题：** 采集能力下沉为独立编译的 C++ 扩展模块（线程池化 + 无锁快照 + 批量写 + no-throw 边界），业务侧用
FastAPI 获得成熟生态（Pydantic 校验、OpenAPI 文档、依赖注入、异步 IO）。**注意**：扩展与 API 同进程，故障隔离靠 `no-throw`
边界与线程自愈实现，而非进程隔离（取舍见 1.5 与 3.3）。

---

## 三、目标总体架构

### 3.1 分层视图

```
┌─────────────────────────────────────────────────────────────────────────┐
│  表现层：Vue3 Web (SPA) · Tauri 2.x 桌面端 · Tauri Android App           │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │ HTTPS / WSS（Nginx 终结 TLS 与静态资源）
┌───────────────────────────────▼─────────────────────────────────────────┐
│  Nginx（openEuler）                                                      │
│   · 静态资源 / 反向代理 / TLS 终止 / WebSocket 升级转发 / 限流             │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │ HTTP / WS（内网）
┌───────────────────────────────▼─────────────────────────────────────────┐
│  FastAPI 业务主框架（Python 3.11+，uvicorn，单进程单 worker）               │
│  ┌────────────┬────────────┬────────────┬────────────┬───────────────┐  │
│  │ 静态字段配置 │ 运行动态配置 │ 采集编排下发 │ 数据/报警/控制│ 鉴权 RBAC/JWT │  │
│  │  base_* API │ running_*  │  Plan 编排   │  时序与台账   │  用户角色      │  │
│  └────────────┴────────────┴────────────┴────────────┴───────────────┘  │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │ 扩展访问层 core/collector/（import industrial_collector，进程内 pybind11 调用） │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │ pybind11（同进程扩展模块，无网络、无 JSON 序列化）
┌───────────────────────────────▼─────────────────────────────────────────┐
│  C++ 采集扩展 industrial_collector._core（被 FastAPI 进程内直接调用）      │
│   · 协议驱动：Modbus TCP/RTU / OPC UA / MQTT / Socket TCP·UDP / WebSocket │
│   · 线程池化采集调度 · 批量读合并 · scale/offset 换算 · 写入执行           │
│   · 只暴露"计划 + 事件 + 采样快照"接口，不含任何业务与 SQL                  │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
┌───────▼────────┐   ┌──────────▼──────────┐   ┌────────▼─────────┐
│ 现场设备/PLC     │   │ PostgreSQL 15        │   │ Redis            │
│ Modbus/OPC UA... │   │ + PostgreSQL        │   │ 缓存/状态/限流     │
└─────────────────┘   └──────────────────────┘   └──────────────────┘
```

### 3.2 关键设计决策

**决策 D1：C++ 采集功能以「Python 扩展模块（pybind11）」交付，由 FastAPI 进程内直接调用，不设独立进程。**

- **唯一形态**：`industrial_collector._core`（Linux/openEuler 上的 CPython 扩展 `.so`，由 `scikit-build-core` + `pybind11`
  构建，`pip install ./backend` 一键安装），**同进程内直接调用**。不部署常驻采集进程、不开监听端口、不走 IPC 序列化。
- **对外唯一契约**：Python 包 `industrial_collector` 导出的 8 个接口（`init` / `apply_plan` / `set_event_callback` /
  `snapshot_since` / `write_point` / `list_protocols` / `health` / `shutdown`），**传 `dict` / `list` / `tuple`，不经 JSON
  字符串**。完整分层契约见《05-pybind11绑定与C++Python分层契约》。
- **为什么不走 C ABI + ctypes**：pybind11 省掉手写 `extern "C"`、`argtypes/restype`、`CFUNCTYPE` 与手工内存管理；异常自动转
  Python 异常带回溯；`std::function` 回调天然强引用（规避 ctypes 回调被 GC 后野指针这个最典型的扩展崩溃点）。代价是扩展与
  Python ABI 绑定（3.11 编的不能给 3.12），因此**构建机与运行机的 Python 小版本必须一致**。
- **为什么是扩展而不是独立服务**：用户明确要求 C++ 作为"FastAPI 可调用的子模块/库"。扩展形态带来三点直接收益：部署只有一套进程体系（systemd
  只管 API 服务）、无 socket 生命周期管理、调用延迟在 µs 级（无序列化进程边界）。代价是 C++ 故障会波及 API 进程，因此必须遵守
  3.3 节的扩展安全约束（no-throw 边界 + 内部线程隔离 + 自愈）。
- **代价的工程对冲**（详见 3.3）：
    1. 所有对外接口内部用 `try/catch(...)` 包住，统一 `py::register_exception` 注册的 6 类异常（4001~4006），**C++
       线程内绝不抛出未捕获异常**（否则 `std::terminate`）；
    2. 第三方协议栈（libmodbus/open62541/libmosquitto）调用全部在驱动层 try/catch + 超时保护内；
    3. 扩展内采集线程与 Python 线程严格分离，所有阻塞调用 `py::gil_scoped_release`（见 05 号 4.4/4.5）；
    4. 进程级守护由 systemd `Restart=always` 兜底，崩溃后 3 秒内自动拉起，配置由 DB 自动重放（见 7.7）。

**决策 D2：C++ 扩展不做 DB、不做 HTTP、不做鉴权。**

现有 `DbHelper.h`、`WebSocketController.h` 的依赖全部从扩展侧移除（与 R2 要求一致）。扩展输入=计划，输出=事件。

**决策 D3：采集调度用"线程池 + 时间轮"取代"每设备一线程"。**

扩展内部维护 I/O 线程池（`IBMCP_KERNEL_IO_THREADS`，默认 = CPU 核数 × 2），按设备亲和性分配，多设备共享线程；每台设备一个
`Plan` 状态对象，由时间轮驱动到期判断。500 台设备不再是 500 线程。

**决策 D4：数据写入改为"扩展写快照 → FastAPI 定时拉取 → 批量落库"。**

扩展把每次采集结果写入**无锁采样快照缓冲**；FastAPI 侧由 `SamplePump` 每 `flush_interval_ms`（默认 200ms）调用
`snapshot_since()` 拉取增量，用 `COPY` 批写 `time_series_data`，报警判定走内存态 + 批量 upsert，不再逐点打 DB。
**采样不走回调推送**（原因见 7.3 与 05 号 3.3）。

**决策 D5：配置下发"推送为主、修订号为辅"。**

FastAPI 在配置变更（激活/停用/子表变更）后立即调用 `collector.apply_plan(plan, revision)`；扩展按 `revision` 幂等（同
revision 重复下发不重建），FastAPI 在启动重放时同样用 `revision` 去重，防止推送丢失。

### 3.3 扩展形态的安全与线程约束（必读）

因为扩展与 FastAPI 同进程，以下五条是**硬约束**，违反即视为缺陷：

| 编号 | 约束                      | 理由 / 做法                                                                                                                                                                                                                                     |
|------|---------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| C1   | **no-throw 边界**         | 所有对外函数内部用 `try/catch(...)`，统一转 `py::register_exception` 注册的异常类（`CollectorError` 基类 + 6 个派生，见 05 号 4.2，带 `code` 属性）。**C++ 线程内（含事件回调、驱动线程）绝不抛出未捕获异常**，否则 `std::terminate` 直接杀进程 |
| C2   | **线程所有权单向**        | 采集线程只属于扩展，由扩展自行创建/回收；Python 侧**永不**在回调线程里执行阻塞操作。回调内仅 `loop.call_soon_threadsafe(queue.put_nowait, obj)`                                                                                                 |
| C3   | **GIL 释放**              | 所有可能阻塞的接口（`init` 建线程、`apply_plan` 校验、`write_point`、`shutdown` join）在 C++ 侧显式 `py::gil_scoped_release`；`snapshot_since` 只持 GIL 拷贝数据，不做 IO 等待                                                                  |
| C4   | **全局单例 + 幂等生命期** | `Collector(...)` 同进程内仅允许成功构造一次（二次构造返回已存在实例或抛 `RuntimeError`）；`shutdown()` 后可再次构造（用于 reload 场景）。用 `std::call_once` + 状态机实现                                                                       |
| C5   | **无静态析构期依赖**      | 禁止在全局/静态对象的析构函数中做协议栈清理（扩展卸载顺序不可控）。清理必须在 `shutdown()` 中显式完成                                                                                                                                           |

**关于 fork 的坑（重要）**：若使用 gunicorn 的 `--preload` 模式，`Collector(...)` 会在 fork 之前执行（Python
模块导入期），导致子进程继承无效的线程状态（线程不随 fork 复制）→ 行为未定义、采集静默不工作。**必须**：`app/main.py` 中扩展正在
lifespan（fork 之后）构造，禁用 `--preload`；或改用 uvicorn 原生 `--workers`（spawn）。此条写入部署检查清单（9.8）。

**GIL 与并发收益的说明**：扩展内采集是纯 C++ 线程，**不持有 GIL**，因此 500 台设备的采集与 Python 事件循环真正的并行；Python
侧只在 `snapshot_since` 取批量快照、事件回调投递、批量落库时占用 GIL，且后者是异步 IO 等待为主。这是"高吞吐"的结构性保证。

---

## 四、静态字段配置与管理（FastAPI，对齐 01 号字典）

### 4.1 静态表的 FastAPI 组织方式

采用 **领域分层**：`schemas/`（Pydantic 模型，一表一文件）+ `repositories/`（SQL 访问）+ `services/`（业务规则）+ `api/v1/`（路由）。

| 静态表（01 号文档）            | Pydantic Schema                 | Repository                           | Service                                | 路由前缀                                   |
|--------------------------------|---------------------------------|--------------------------------------|----------------------------------------|--------------------------------------------|
| `base_device_base_info`        | `schemas/static/device_base.py` | `repositories/static/device_repo.py` | `services/static/device_service.py`    | `/api/v1/static/devices`                   |
| `base_device_category_dict`    | `.../device_category.py`        | `.../dict_repo.py`                   | `services/static/dict_service.py`      | `/api/v1/static/categories`                |
| `base_device_brand_model_dict` | `.../brand_model.py`            | `.../dict_repo.py`                   | 同上                                   | `/api/v1/static/brands`                    |
| `base_device_port_dict`        | `.../port.py`                   | `.../dict_repo.py`                   | 同上                                   | `/api/v1/static/ports`                     |
| `base_device_protocol_dict`    | `.../protocol.py`               | `.../dict_repo.py`                   | 同上                                   | `/api/v1/static/protocols`                 |
| `base_device_data_item_dict`   | `.../data_item.py`              | `.../data_item_repo.py`              | `services/static/data_item_service.py` | `/api/v1/static/data-items`                |
| `base_process_route_resource`  | `.../route_resource.py`         | `.../route_repo.py`                  | `services/static/route_service.py`     | `/api/v1/static/routes`                    |
| `base_process_route_step`      | `.../route_step.py`             | `.../route_step_repo.py`             | 同上                                   | `/api/v1/static/routes/{route_code}/steps` |
| `base_equipment_base_info`     | `.../equipment_base.py`         | `.../equipment_repo.py`              | `services/static/equipment_service.py` | `/api/v1/static/equipments`                |
| `base_equipment_function_dict` | `.../equipment_function.py`     | `.../function_repo.py`               | 同上                                   | `/api/v1/static/equipment-functions`       |

**统一语法**（每张表自动获得六件套，由通用基类 `StaticDictRouter` 生成）：

```
GET    {prefix}            分页列表（page / size / keyword + 各表专属筛选）
GET    {prefix}/all        全量下拉（配置向导用）
GET    {prefix}/{id}       详情
POST   {prefix}            新增（Pydantic 校验 + 唯一键冲突 → 409）
PUT    {prefix}/{id}       更新
DELETE {prefix}/{id}       删除（被引用 → 409，附引用表与引用数）
```

### 4.2 Pydantic 校验规则（相对现有实现的升级点）

历史方案仅做 SQL 层约束，FastAPI 版要求**入参即校验**：

```python
class BaseDeviceCreate(BaseModel):
    model_config = ConfigDict(str_strip_whitespace=True, extra="forbid")

    device_code: str = Field(min_length=1, max_length=64, pattern=r"^[A-Za-z0-9_-]+$")
    device_name: str = Field(min_length=1, max_length=128)
    device_category_code: str | None = Field(default=None, max_length=64)
    brand_code: str | None = Field(default=None, max_length=64)
    model_code: str | None = Field(default=None, max_length=64)
    serial_number: str | None = Field(default=None, max_length=128)
    manufacturer: str | None = Field(default=None, max_length=128)
    supported_protocols: list[str] = Field(default_factory=list)   # DB 存 JSONB
    supported_ports: list[str] = Field(default_factory=list)       # DB 存 JSONB
    data_items: list[DataItemRef] = Field(default_factory=list)    # 对外开放采集数据项
    description: str | None = None
```

要点：

1. **JSON 字段用 `list/dict` 而非字符串**：`supported_protocols`、`supported_ports`、`data_items`、`step_params`、
   `device_connection`、`route_params`、`function_params` 全部在 Schema 层声明为结构化类型，落库时由 Repository 统一
   `json.dumps(...)` → `::jsonb`，出参自动反序列化。解决现有 C++ 侧"`::text` 读 / `::jsonb` 写"的手工约定容易出错的问题。
2. **跨表一致性校验**：`device_category_code` 必须存在于 `base_device_category_dict`；`protocol_code` 必须存在于
   `base_device_protocol_dict` 且必须是扩展 `Collector.list_protocols()` 支持值；`step_code` 必须在
   `(route_code, step_code)` 联合唯一内存在。
3. **`extra="forbid"`**：前端多传字段直接 422，避免脏字段静默丢弃。
4. **错误模型统一**：沿用现有成功/失败语义（`code=0` 成功），HTTP 层再叠加标准错误体，见 5.5 节。

### 4.3 静态字典的"选项数据源"接口

配置向导需要一次性拉取全部下拉项。FastAPI 提供聚合端点（避免前端 10 次请求）：

```
GET /api/v1/static/bootstrap
→ { categories[], brands[], ports[], protocols[], data_items[], equipment_functions[], routes[] }
```

内部并行查询（`asyncio.gather` + 连接池），带 60s Redis 缓存键 `ibmcp:static:bootstrap`，字典变更时按前缀失效。

---

## 五、运行动态配置字段管理（FastAPI，对齐 02 号字典）

### 5.1 五级配置链的 API 映射

02 号文档定义的五级链
`running_equipment_config → running_equipment_route → running_route_device → running_device_collection` 与
`running_equipment_function` 分支，对应 FastAPI 路由如下：

| 层级              | 表                           | Service                       | 路由                                                      |
|-------------------|------------------------------|-------------------------------|-----------------------------------------------------------|
| L1 装备运行实例   | `running_equipment_config`   | `RunningConfigService`        | `/api/v1/config/equipments`（CRUD + `/{id}/expand`）      |
| L2a 装备→工艺路线 | `running_equipment_route`    | `RunningRouteService`         | `/api/v1/config/equipments/{config_id}/routes`            |
| L2b 装备→功能     | `running_equipment_function` | `RunningFunctionService`      | `/api/v1/config/equipments/{config_id}/functions`         |
| L3 工序→设备绑定  | `running_route_device`       | `RunningDeviceBindingService` | `/api/v1/config/routes/{route_config_id}/devices`         |
| L4 设备→采集项    | `running_device_collection`  | `RunningCollectionService`    | `/api/v1/config/device-bindings/{binding_id}/collections` |

> 前缀口径以 03 号文档 1.2 为准：配置类统一 `/api/v1/config/*`，运行态查询与控制统一 `/api/v1/runtime/*`。

### 5.2 关键接口：级联展开与"下发预览"

- `GET /api/v1/config/equipments/{id}/expand` → `config + routes[] + functions[]`（对齐现有
  `EquipmentConfigController::expand`）
- `GET /api/v1/config/routes/{id}/expand` → `bindings[] + 每个 binding.collections[]`
- **新增 `GET /api/v1/config/equipments/{id}/plan`** → **把五级配置链编译成一份"采集计划预览"**（即将要下发给 C++ 扩展的
  `plan`，见 7.2 节）。这是本次重构新增的核心能力：**配置与下发的解耦点**
  ，前端可先看"将要采什么、用哪个协议、周期多少、地址是什么"，再点确认。

配置链 → 采集计划的编译规则（`services/plan_compiler.py`）：

```
running_equipment_config (config_status = 1)
  └─ running_equipment_route (enabled = 1)
       └─ running_route_device (enabled = 1)
            │  protocol_code ← 覆盖 base_device_protocol_dict 的默认值
            │  device_connection ← 覆盖 base_device_base_info 的默认连接参数
            └─ running_device_collection (enabled = 1)
                 │  address = COALESCE(NULLIF(address_override,''), data_item.address_rule)
                 │  cycle   = COALESCE(collection_cycle, data_item.collection_cycle)
                 │  data_type / read_write ← base_device_data_item_dict
                 ▼
              DevicePlan{ device_key, protocol, conn, points[] }
```

未纳入计划的配置（`enabled=0` 或 `config_status≠1`）一律不下发，对应设备在运行期自然离线。

### 5.3 配置状态机与生效动作

沿用 02 号文档 `config_status` 语义，但**生效由 FastAPI 主动触发扩展，而非数据库触发器**：

```
配置保存（未激活 0）──► 预览 plan（不生效）
        │
        ├── 激活（1）──► PlanCompiler 编译 → 版本号+1 → collector.apply_plan(plan, rev) → 等待 plan_ack
        │                                    └─ 失败 → 保留旧版本，返回 409 + extension_error
        │
        └── 停用（2）──► 重编 plan（排除该 config）→ collector.apply_plan(plan, rev) → 扩展断开该设备 → 清理运行态（不删历史数据）
```

**相对现有实现的改造**：现有 `db/01-init.sql` 中用触发器 `trg_running_config_sync` + `sync_config_to_using_tables()` 做
`running_* → using_*` 同步。FastAPI 版**取消该触发器链**，改为：

1. `using_devices` / `using_points` 降级为**扩展运行态快照表**（由 FastAPI 在 `plan_ack` 后写入，仅供查询/联表/历史关联），不再作为配置真相源。
2. 配置真相源 = `running_*` 五张表 + `plan` 版本记录（新增 `runtime_plan_revision` 表，见 6.3 节）。
3. 这样"配置 → 计划 → 扩展"链路单向、可审计、可回滚，避免触发器双向同步造成的不一致。

### 5.4 配置变更审计

新增轻量表 `audit_config_change`（不改动 01/02/03 号文档已定义的业务表）：

| 字段                 | 类型         | 说明                                                                                                 |
|----------------------|--------------|------------------------------------------------------------------------------------------------------|
| id                   | BIGSERIAL PK | 主键                                                                                                 |
| target_level         | VARCHAR(16)  | `equipment_config` / `equipment_route` / `equipment_function` / `route_device` / `device_collection` |
| target_id            | BIGINT       | 对应运行表的 id                                                                                      |
| action               | VARCHAR(16)  | `create` / `update` / `delete` / `activate` / `deactivate`                                           |
| plan_revision_before | BIGINT       | 变更前计划版本                                                                                       |
| plan_revision_after  | BIGINT       | 变更后计划版本（未触发下发时为 NULL）                                                                |
| operator             | VARCHAR(64)  | 操作人                                                                                               |
| detail               | JSONB        | 变更差异（字段级 diff）                                                                              |
| created_at           | BIGINT       | Unix 秒                                                                                              |

### 5.5 统一响应体（对齐现有 `Result` 语义）

为兼容现有前端（当前接收 `{code, message, data}`），FastAPI 统一包一层：

```python
@app.middleware("http")
async def wrap_result(request, call_next): ...
```

| 场景                 | HTTP | 响应体                                                                              |
|----------------------|------|-------------------------------------------------------------------------------------|
| 成功                 | 200  | `{"code": 0, "message": "ok", "data": ...}`                                         |
| 校验失败（Pydantic） | 422  | `{"code": 422, "message": "validation error", "data": {"errors": [...]}}`           |
| 唯一键/外键冲突      | 200  | `{"code": 409, "message": "device_code already exists", "data": {...}}`             |
| 未找到               | 200  | `{"code": 404, "message": "...", "data": null}`                                     |
| 扩展下发失败         | 200  | `{"code": 409, "message": "apply_plan failed", "data": {"extension_error": "..."}}` |
| 未捕获异常           | 200  | `{"code": 500, "message": "internal error", "data": null}`                          |

> 分页结构保持与现有约定一致：`data = {"items": [...], "total": N}`；分页参数 `page` / `size` / `keyword` 不变。
> **这意味着前端现有的调用代码与响应解析逻辑可以零改动迁移**，只需替换 base URL。

---

## 六、FastAPI 侧工程结构

### 6.1 目录结构（单仓多模块）

```
01-IBMCP/
├── backend/                        # 新增：FastAPI 业务主框架
│   ├── pyproject.toml                 # 依赖与工具配置（uv / poetry 均可）
│   ├── .python-version                # 3.11.x
│   ├── alembic.ini
│   ├── migrations/                    # Alembic 版本（替代 01-init.sql 的增量演进）
│   ├── app/
│   │   ├── main.py                    # create_app()：装配路由、中间件、生命周期
│   │   ├── core/
│   │   │   ├── config.py              # Settings（pydantic-settings，读 .env / 环境变量）
│   │   │   ├── logging.py             # structlog JSON 日志（含 request_id）
│   │   │   ├── security.py            # JWT / 密码哈希（argon2）
│   │   │   ├── deps.py                # 依赖注入：db_pool / redis / collector / current_user
│   │   │   └── errors.py              # 业务异常 → Result 包膜
│   │   ├── db/
│   │   │   ├── pool.py                # asyncpg 连接池（单进程一个 pool）
│   │   │   ├── models/                # 可选：SQLAlchemy 2.0 映射（只读查询用）
│   │   │   └── sql/                   # 手写 SQL（批量 COPY / 聚合查询）
│   │   ├── schemas/
│   │   │   ├── common.py              # PageParams / ResultEnvelope / ErrorItem
│   │   │   ├── static/                # 对齐 01 号文档，一表一文件
│   │   │   └── running/               # 对齐 02 号文档，一表一文件
│   │   ├── repositories/              # 纯 SQL 访问层（asyncpg，不用 ORM 做写路径）
│   │   ├── services/
│   │   │   ├── static/                # 静态字典业务
│   │   │   ├── running/               # 运行动态配置业务
│   │   │   ├── plan_compiler.py       # running_* → 采集计划
│   │   │   ├── collector_orchestrator.py  # 下发/回执/修订号管理
│   │   │   ├── ingest_service.py      # 批量时序写入 + 报警判定
│   │   │   ├── control_service.py     # 控制命令台账与下发
│   │   │   └── query_service.py       # 历史/聚合查询
│   │   ├── api/v1/
│   │   │   ├── router.py              # 汇总 include_router
│   │   │   ├── static_dict.py         # /static/*
│   │   │   ├── running_config.py      # /config/*（装备/路线/绑定/采集项）
│   │   │   ├── data.py                # /data/*
│   │   │   ├── devices.py             # /runtime/devices/*（运行态查询 + 控制）
│   │   │   ├── alarms.py              # /alarms/*
│   │   │   ├── auth.py                # /auth/*
│   │   │   ├── collector_admin.py     # /api/v1/collector/*（状态/协议清单/健康）
│   │   │   └── ws.py                  # /ws/collect（单端点，四固定主题）
│   │   ├── core/collector/             # 扩展访问层（ffi.py / stub.py / pump.py / protocol.py）
│   │   │   ├── protocol.py            # CollectorApi Protocol（唯一契约定义）
│   │   │   ├── ffi.py                 # 真实扩展薄封装（import industrial_collector）
│   │   │   └── stub.py                # 桩实现（Windows/无设备，校验规则与扩展一致）
│   │   └── ws/
│   │       ├── hub.py                 # 连接注册表 + 按 topic 广播
│   │       └── topics.py              # data / device / alarm / control 四主题
│   └── tests/
│       ├── unit/                      # plan_compiler / stub 契约测试
│       └── api/                       # httpx.AsyncClient 接口测试
│
├── collector-cpp/                     # C++ 采集扩展源码（从现有 backend/ 精简而来，最终编译为 _core 扩展）
│   ├── CMakeLists.txt                 # 被 scikit-build-core 调用，产出 _core 扩展模块
│   ├── include/collector/             # 扩展内部 C++ 头文件（非对外交付物，Python 侧不可见）
│   ├── src/
│   │   ├── bindings/module.cpp        # pybind11 绑定层（对外唯一入口）
│   │   ├── collector.cpp              # Collector 门面：串起 scheduler / drivers / store
│   │   ├── scheduler/                 # 时间轮 + IO 线程池（timewheel.cpp / io_pool.cpp）
│   │   ├── drivers/                   # 现有驱动原样保留（去掉 DB/WS 依赖）+ DriverFactory
│   │   ├── store/                     # DataStore：无锁采样快照缓冲（snapshot_since）
│   │   ├── plan/                      # plan_parser / plan_validator（结构解析 + rc 校验）
│   │   └── common/                    # event_emitter（状态事件）+ logger
│   └── tests/                         # GoogleTest：驱动 mock + 调度器 + store 测试
│
├── python/
│   └── industrial_collector/           # 对外 Python 包（wheel.packages 指向此处）
│       ├── __init__.py                 # 再导出 Collector 等 8 个接口 + 6 个异常类
│       ├── protocol.py                 # CollectorApi Protocol（与 app/core/collector/protocol.py 同源）
│       └── py.typed
│
├── frontend/                           # 新增：Vue3 前端（Web + Tauri 桌面 + Android，一套代码三形态）
│   ├── package.json                    # vue / vite / element-plus / echarts / pinia / tauri 等
│   ├── src/                            # 业务层（页面 / Store / API / WS / 组件），三形态 100% 共用
│   ├── src/adapters/                   # 多端适配层（HTTP / WS / 系统能力，按运行形态注入）
│   └── src-tauri/                      # Tauri 2.x 壳（桌面 Windows/Linux + Android）
│
├── pyproject.toml                      # scikit-build-core + pybind11 构建配置
│
├── packaging/
│   ├── systemd/
│   │   └── ibmcp-api.service          # 唯一的服务单元：uvicorn（扩展随进程 import）
│   ├── nginx/ibmcp.conf
│   └── docker/                        # 可选：openeuler 基础镜像构建
│
├── deployments/
│   ├── openeuler/bootstrap.sh         # 欧拉系统一键装 PG15/Redis/Nginx/运行时
│   ├── openeuler/build.sh             # 欧拉侧 pip install -e ./backend（编译 _core）
│   └── dev-windows/bootstrap.ps1      # Windows 本地依赖（Python venv / lint 工具）
│
└── 01-docs/                           # 文档（本文件位于 FASTAPI架构/ 下）
```

### 6.2 应用装配（`app/main.py` 要点，不含业务实现）

```python
def create_app() -> FastAPI:
    lifespan = Lifespan()            # start: pool / redis / collector / pump / hub
                                     # stop:  pump.stop() / collector.shutdown() / pool.close()
    app = FastAPI(title="IBMCP", lifespan=lifespan, openapi_url="/api/v1/openapi.json")
    app.include_router(api_v1_router, prefix="/api/v1")
    app.add_middleware(CORSMiddleware, allow_origins=settings.cors_origins, ...)
    app.add_middleware(ResultEnvelopeMiddleware)   # 统一 {code,message,data}
    return app
```

**生命周期顺序（关键）**：

1. 建 `asyncpg.Pool`（`min_size=5, max_size=20`）
2. 建 Redis 连接池
3. `collector = Collector(cfg)`（**必须在 fork 之后**）→ `collector.set_event_callback(python_cb)`
4. `CollectorOrchestrator.bootstrap()`：对比 DB 最新 `plan_revision` 与即将下发的 revision，不一致则 `apply_plan()` 并等待
   `plan_ack`
5. `SamplePump.start()`：每 200ms `collector.snapshot_since()` → 攒批 → `COPY` 落库 → 报警 → WS 广播
6. 关闭时逆序：停止 pump → 冲刷剩余快照 → `collector.shutdown()` → 关池

### 6.3 新增运行时表（最小增量）

| 表                      | 用途                                                              | 主要字段                                                                                                                                                            |
|-------------------------|-------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `runtime_plan_revision` | 记录每次下发的计划版本                                            | `revision BIGSERIAL PK, config_id BIGINT, plan_hash VARCHAR(64), status SMALLINT(0=已下发1=生效2=失败), extension_error TEXT, created_at BIGINT, applied_at BIGINT` |
| `audit_config_change`   | 见 5.4 节                                                         | 见上                                                                                                                                                                |
| `using_device_runtime`  | 扩展回执的设备运行态快照（替代被降级的 `using_devices` 部分职责） | `device_key VARCHAR(96) PK, protocol_code, status SMALLINT(0=离线1=在线2=异常), last_error TEXT, points_ok INT, points_fail INT, snapshot_at BIGINT`                |

> `time_series_data`、`using_alarms`、`control_commands`、RBAC 三表**字段结构不变**，见 03 号文档。

---

## 七、FastAPI ⇄ 采集扩展接口契约（核心）

> **本节是 05 号《pybind11绑定与C++Python分层契约》的摘要视图**。两者冲突时**以 05 号为准**
> ；本节只保留"跨文档引用所需的接口清单与载荷形状"，完整实现细节（构建、GIL、异常、单元测试）全部在 05 号。

### 7.1 扩展交付形态与加载方式

**唯一形态：Python 扩展模块（pybind11），FastAPI 进程内 `import` 加载。**

| 项目            | 说明                                                                                         |
|-----------------|----------------------------------------------------------------------------------------------|
| 产物            | `industrial_collector._core`（CPython 扩展 `.so`，由 `scikit-build-core` + `pybind11` 构建） |
| 对外包          | `industrial_collector`（`__init__.py` 再导出 8 个接口），FastAPI 只 `import` 这个包          |
| 加载            | `from industrial_collector import _core`，`app/core/collector/ffi.py` 做一层薄封装           |
| 依赖            | libmodbus / open62541 / libmosquitto 链接进扩展；`auditwheel`/`LD_LIBRARY_PATH` 保证可解析   |
| 符号可见性      | `py::module_local()` + 不导出裸 C 符号，避免污染进程符号表                                   |
| 版本协商        | `health()` 返回 `{"abi":"1.2","lib":"1.2.3","protocols":[...]}`，Python 启动时校验 ABI       |
| Python 版本绑定 | 扩展与 CPython ABI 绑定，**构建机与运行机 Python 小版本必须一致**（3.11 编的不能用于 3.12）  |

**Python 接口清单（`industrial_collector/__init__.py` 导出 8 个，签名为权威）**

```python
class Collector:
    def __init__(self, cfg: dict) -> None: ...
    def init(self, cfg: dict) -> None: ...               # 幂等；二次调用返回已存在实例
    def health(self) -> dict: ...                        # {"abi","lib","protocols","threads",...}
    def list_protocols(self) -> list[str]: ...           # 替代 DriverFactory::listAvailable()
    def protocol_capabilities(self) -> dict[str, dict]: ...  # 各驱动能力（可写/最大点数等）
    def apply_plan(self, plan: dict, revision: int) -> int: ...  # 0=成功；校验失败抛 CollectorError
    def set_event_callback(self, cb: Callable[[dict], None] | None) -> None: ...
    def snapshot_since(self, since_seq: int) -> tuple[int, list[SampleOut]]: ...  # 游标进、游标出
    def write_point(self, device_key: str, point_id: int, value: str) -> int: ...  # 返回 command_id
    def shutdown(self) -> None: ...                      # 幂等；先释放 GIL 再 join
```

> **核心方法仍是 8 个**：`init` / `apply_plan` / `set_event_callback` / `snapshot_since` / `write_point` /
> `list_protocols` / `health` / `shutdown`。`protocol_capabilities()` 是**能力查询的补充**（04 号第六章），与
> `list_protocols()` 职责分离：前者回答"有哪些协议"，后者回答"某协议能不能写、最大多少点"。签名细节以 05 号 8 章为准。

> **不再有** `char**` 出参、`ibmcp_kernel_free`、头文件内存所有权约定 —— 这些是 C ABI 形态的产物，pybind11 下由 Python
> 侧对象生命周期管理。
>
> **不再有** `validate_plan` / `remove_plan` / `stats` 三个方法：plan 预览由 `plan_compiler` 在 Python
> 侧做结构校验（不跨边界），计划撤销通过"重编 plan + `apply_plan` 新 revision"表达，`stats` 合并进 `health()` 与
> `collector_stats` 事件。

**为什么选 pybind11，而不是 C ABI + JSON？**

1. **零序列化开销**：`dict`/`tuple`/`list` 直接跨边界，5000 点/秒的采样通路不再做两次 JSON 编解码（这是吞吐关键路径）。
2. **异常与追溯完整**：C++ 异常经 `py::register_exception` 转成 Python 异常，带 `file:line` 与 `what()`
   ，日志可读性远优于"返回码 + err_json"。
3. **回调安全**：`std::function` 持有 Python 可调用对象的强引用，**从根本上消除** ctypes 回调被 GC 后野指针这个扩展形态最致命崩溃点。
4. **构建一体化**：`../../pyproject.toml` + `pip install ./backend` 一条命令完成"编译 + 安装 + 可 import"，欧拉侧不必再做
   `cmake --install` + `ldconfig` + 路径拼接。

**代价与对冲**：扩展与 Python ABI 绑定（不能跨小版本复用）、调试需附着到 Python 进程。对冲方式见 05 号 7.3（Windows 用 stub）与
10.2（gdbserver attach）。

### 7.2 下发契约：采集计划（Plan）

**语义：`Collector.apply_plan(plan, revision) -> int`**（0=成功；非 0 或抛异常=失败，错误体见 5.5 `extension_error`）

```json
{
  "revision": 1024,
  "flush_interval_ms": 200,
  "devices": [
    {
      "device_key": "DEV-0001",
      "device_name": "1# 空压机",
      "equipment_code": "EQ-0001",
      "protocol": "modbus_tcp",
      "connection": { "host": "192.168.1.10", "port": 502, "slave": 1 },
      "connect_timeout_ms": 3000,
      "read_timeout_ms": 1000,
      "points": [
        {
          "point_key": "DEV-0001:40001",
          "point_id": 21,
          "address": "40001",
          "data_type": "float",
          "scale": 1.0,
          "offset": 0.0,
          "unit": "℃",
          "cycle_ms": 1000,
          "read_write": "R",
          "function_code": 3,
          "register_count": 2,
          "alarm_high": 120.0,
          "alarm_low": -10.0,
          "alarm_enabled": true
        }
      ]
    }
  ]
}
```

| 字段                                                | 说明                                                                     | 来源（02 号文档）                                                                               |
|-----------------------------------------------------|--------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------|
| `revision`                                          | 计划版本号，扩展据此幂等：相同 revision 重复下发不重建                   | `runtime_plan_revision.id`                                                                      |
| `device_key`                                        | 设备业务键（等同于 `device_code`）                                       | `running_route_device.device_code`                                                              |
| `protocol`                                          | 协议名，必须 ∈ `list_protocols()`；`websocket` 自动归一化为 `web_socket` | `base_device_protocol_dict.protocol_code`（可被运行期覆盖）                                     |
| `connection`                                        | 协议相关连接参数，JSON 结构因协议而异                                    | `running_route_device.device_connection` ← 覆盖 `base_device_base_info` 默认                    |
| `points[].address`                                  | 通信地址；**空值直接拒绝**（禁止回退 `item_code`）                       | `running_device_collection.address_override` ← 空时取 `base_device_data_item_dict.address_rule` |
| `points[].cycle_ms`                                 | 采集周期（毫秒）                                                         | `running_device_collection.collection_cycle`（秒）* 1000                                        |
| `points[].scale / offset / alarm_* / alarm_enabled` | 换算与阈值，**随 plan 下发**，扩展做工程值换算                           | `base_device_data_item_dict` 5 新增列 ← 可被 `running_device_collection` 同名列覆盖             |
| `points[].point_id`                                 | FastAPI 侧点位主键，用于回传关联                                         | 生成时由 FastAPI 分配                                                                           |

**失败语义**：不再返回错误码 + err_json，而是**抛出扩展异常**（`CollectorError` 及 6 个派生类，带 `rc` 与 `stage`/
`device_key`/`point_id` 定位）。**码值语义以 05 号 8.1 为唯一权威**：4001 协议不支持 / 4002 连接参数非法（含 `scale=0`、
`address` 为空）/ 4003 plan 版本冲突 / 4004 扩展未初始化或设备不在 plan / 4005 控制队列已满 / 4006 写入目标不在
plan。FastAPI 侧经 `to_api_error()` 统一透传为 HTTP 200 + 业务 `code`（见 03 号 2.3），不再二次包装成 4xx。

> **注意**：`apply_plan` 只做**校验与注册**，立即返回（毫秒级）；真正的连接与采集在后台异步发生，结果通过采样快照 +
> 事件回调回传。这是保证"HTTP 接口不被设备 IO 阻塞"的关键。

### 7.3 反馈契约：事件流（Event Stream）

库 → FastAPI 的事件（`dict`，经 `set_event_callback` 送达）。**字段契约以 05 号 6.3 为唯一权威**：

| 事件类型          | 触发               | 关键字段                                                                            |
|-------------------|--------------------|-------------------------------------------------------------------------------------|
| `device_status`   | 在线/离线/异常切换 | `device_key`, `status`（0=离线 1=在线 2=异常）, `reason`, `ts_ms`                   |
| `write_result`    | 控制写入执行完成   | `command_id`, `device_key`, `point_id`, `success`, `message`, `elapsed_ms`, `ts_ms` |
| `plan_ack`        | 计划生效或失败     | `revision`, `status`, `device_count`, `error`, `ts_ms`                              |
| `collector_stats` | 周期（默认 1s）    | `threads`, `plans`, `points_read`, `points_fail`, `queue_depth`, `dropped`, `ts_ms` |

> **扩展侧一律 `ts_ms`（毫秒整数），不写 `ts`**。秒级 `ts` 是 03 号 5.2 WS 投影字段，只在 Python 的 `EventDispatcher` 里换算一次。
> **驼峰键在扩展边界内一律不出现**。

**采样数据不走事件回调，走拉取**（05 号 3.3 核心决策）：

```python
# 事件回调：只投递，零阻塞
def _on_event(ev: dict) -> None:
    loop.call_soon_threadsafe(event_queue.put_nowait, ev)

collector.set_event_callback(_on_event)

# 采样拉取泵：每 200ms 拉一次增量快照（游标由 pump 独占）
async def sample_pump():
    since_seq = 0
    while running:
        await asyncio.sleep(0.2)
        new_seq, samples = await asyncio.to_thread(collector.snapshot_since, since_seq)
        since_seq = new_seq          # 仅成功后推进（失败重试，见 05 号 5.5）
        if samples:
            buf.extend(samples)
        if len(buf) >= 500:
            await copy_into_timeseries(buf)
            await alarm_engine.evaluate(buf)
            await ws_hub.broadcast_samples(buf)
            buf.clear()
```

> **为什么采样用"拉"而非回调推**：C++ 线程回调 Python 必须取 GIL，5000 点/秒的高频推送会造成 GIL
> 抖动；改成"扩展写无锁快照 → Python 定时批量取"后，跨边界次数从 5000 次/秒降到 5 次/秒，且批量接口天然对齐 `COPY`
> 攒批。事件（状态类，低频）仍走回调，保持实时性。

**回调红线**：回调在 **C++ 采集线程**上执行，**只允许** `loop.call_soon_threadsafe` 投递，**禁止**在回调内做 DB / HTTP /
加锁等待，否则会拖慢采集线程并形成反压。

### 7.4 控制链路（写点）

```
POST /api/v1/runtime/devices/{device_key}/control  { point_id, value }
  │
  ├─ ControlService::issue()   → 写 control_commands（status=0 待下发），返回 command_id
  ├─ collector.write_point(device_key, point, command_id)  → 立即返回"已入队"
  └─ 库执行 → 事件 write_result → 回写 control_commands（status=2/3/4） → WS 广播 control
```

**硬约束（04 号 253 行）**：`write_point` **只对 plan 内已注册的点位生效**。禁止旧版 `DeviceController` 的"计划外设备临时
`createByName → connect → write → disconnect`"兜底路径 —— 该路径绕过连接复用与超时保护，是设备侧连接被占用的主要来源。

**接口在语义上是"异步确认"**：HTTP 返回即表示"已受理"，最终执行结果通过 `write_result` 事件 + WS 推送到达。若需要同步等待，FastAPI
侧提供可选 `wait=true` 参数（最长 5s，内部 `asyncio.Event` 等待对应 `command_id` 的 `write_result`）。

### 7.5 管理类接口

| 接口                                 | 用途                                                                                                                                                                            |
|--------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `Collector.list_protocols()`         | 返回编译期实际可用的协议名列表（替代现有 `DriverFactory::listAvailable()`）                                                                                                     |
| `Collector.health()`                 | ABI 与扩展版本、线程数、协议清单、采集统计（`threads` / `plans` / `points_read` / `points_fail` / `queue_depth` / `dropped`），启动期校验与 `/api/v1/collector/health` 的数据源 |
| `Collector.set_event_callback(None)` | 解绑回调（先解绑再 `shutdown`，避免关停期回调进已关闭的事件循环）                                                                                                               |
| `Collector.shutdown()`               | 停止调度、断开全部连接、释放资源（可再次 `init`）                                                                                                                               |

> **计划撤销不需要 `remove_plan`**：停用配置 = 重编 plan（排除该 config）→ `apply_plan(new_plan, new_revision)`，扩展看到新
> revision 后自然断开不在 plan 内的设备。这比"增量撤销某个版本"更简单，也避免多版本共存时的状态歧义。

### 7.6 构建与安装（pybind11 形态）

**一条命令完成编译与安装（欧拉侧）**

```bash
pip install -e ./backend        # scikit-build-core 自动调 CMake 编译 _core 并安装
```

**`backend/pyproject.toml` 关键配置**（完整版见 05 号 7.1/7.2）

```toml
[build-system]
requires = ["scikit-build-core>=0.10", "pybind11>=2.12"]
build-backend = "scikit_build_core.build"

[project]
name = "industrial-collector"
requires-python = ">=3.11"

[tool.scikit-build]
cmake.source-dir = "collector-cpp"
wheel.packages = ["python/industrial_collector"]
```

**关键约束**

| 项                          | 要求                                                         | 原因                                                      |
|-----------------------------|--------------------------------------------------------------|-----------------------------------------------------------|
| `POSITION_INDEPENDENT_CODE` | 必须 ON                                                      | 扩展模块硬性要求（pybind11 默认已开）                     |
| `--no-undefined`            | 必须开                                                       | 链接期暴露缺失符号，避免 `import` 时报 `undefined symbol` |
| 不链接 `libpython`          | Linux 下**必须不链**（`-undefined dynamic_lookup` 仅 macOS） | 扩展由解释器解析 Python 符号                              |
| `-D_GLIBCXX_USE_CXX11_ABI`  | 与 openEuler gcc 默认 ABI 一致                               | 避免与协议库混链时 `std::string` 符号错位                 |
| Python 版本                 | 构建机 = 运行机（小版本一致）                                | CPython ABI 绑定                                          |

**安装位置**：走 wheel/`pip install` 进 venv 的 `site-packages`（唯一推荐方式），不再使用 `/usr/local/lib` + `ldconfig` +
`IBMCP_KERNEL_LIB` 路径拼接；`LD_LIBRARY_PATH` 只需保证协议库（libmodbus 等）可解析。

### 7.7 崩溃自愈与配置重放

扩展形态下 API 进程即采集载体，因此把"崩溃恢复"设计成**无状态重启**：

```
systemd: Restart=always, RestartSec=3
   │
   ▼
uvicorn 拉起 → lifespan.start
   ├─ Collector(cfg)                （空计划，什么也不采）
   ├─ 读 runtime_plan_revision 中 status=生效 的最新记录
   ├─ collector.apply_plan(该 plan)
   └─ 收到 plan_ack → 记录 applied_at → 开始采集
```

**要点**：进程崩溃不需要人工干预，重启后 3~5 秒自动恢复采集；期间产生的空档在 `time_series_data` 中体现为缺口（不补采）。

### 7.8 覆盖率测试策略（不依赖真实设备）

| 测试层          | 手段                                                                 | 覆盖目标                                      |
|-----------------|----------------------------------------------------------------------|-----------------------------------------------|
| 驱动单元测试    | GoogleTest + mock 协议客户端（`FakeModbusClient`/`FakeOpcUaClient`） | 编解码、scale/offset、超时分支                |
| 调度器测试      | 注入虚拟时钟（时间轮可推进）                                         | 周期分组、批量合并、背压丢弃                  |
| 扩展接口测试    | `pytest` 直接 import `_core`，无 DB、无网络                          | 接口签名、异常码 4001~4006、`shutdown()` 幂等 |
| Python 集成测试 | `pytest` + 已安装扩展 + stub 设备                                    | `apply_plan → snapshot_since → 落库` 全链路   |
| 端到端压测      | Python 起虚拟 Modbus 从站（500 台/5000 点），打真实扩展              | 吞吐、延迟、内存增长                          |

---

## 八、多线程 / 高并发 / 高吞吐设计

### 8.1 并发模型全景

```
                       ┌──────────────────────────────────────────┐
 HTTP 请求 ───────────►│ uvicorn worker (asyncio event loop × N)   │
 WS 连接   ───────────►│  · 全异步 IO（asyncpg / redis）           │
                       │  · 业务逻辑禁阻塞（无同步 DB 调用）         │
                       │  · 调用扩展时阻塞部分释放 GIL（py::gil_scoped_release）│
                       └──────────────┬───────────────────────────┘
                                      │ 事件队列（asyncio.Queue，线程安全投递）
                       ┌──────────────▼───────────────────────────┐
                       │ IngestConsumer 协程（每 worker 1 个）     │
                       │  · 拉取 snapshot_since → 攒批 → COPY → 报警 → 广播 │
                       └──────────────┬───────────────────────────┘
                                      │ 进程内 Python 调用（同进程，µs 级）
                       ┌──────────────▼───────────────────────────┐
                       │ C++ 采集扩展 industrial_collector._core    │
                       │  · I/O 线程池（默认 CPU×2，可配）          │
                       │  · 时间轮调度器（毫秒精度，无 per-device 线程）│
                       │  · 每设备 Plan 状态 + 连接复用             │
                       │  · 无锁采样快照缓冲（snapshot_since 读取）  │
                       │  · 持有 C++ 线程，不持 GIL → 真并行         │
                       └──────────────────────────────────────────┘
```

> **GIL 说明**：扩展的采集线程完全不持有 GIL（C++ 线程从不调用 Python API 除事件回调），与 Python 事件循环物理并行；Python
> 仅在"`snapshot_since` 取批量快照"与"批量落库"时占用 GIL，且后者以异步 IO 等待为主。事件回调取 GIL 的时间被压到 µs 级（
> `call_soon_threadsafe` 投递即返回）。因此 `workers × 事件循环` 与 `IO 线程池` 构成两条互不争抢的执行面。

### 8.2 三层吞吐保障措施

**① HTTP 层（FastAPI）**

| 措施       | 说明                                                                                                | 配置                |
|------------|-----------------------------------------------------------------------------------------------------|---------------------|
| 多 worker  | `uvicorn --workers N`（N ≈ CPU 核数，或 `gunicorn -k uvicorn.workers.UvicornWorker`）               | `IBMCP_API_WORKERS` |
| 全异步驱动 | `asyncpg`（非 psycopg2 同步），`redis.asyncio`                                                      | —                   |
| 连接池     | 每 worker 一个 `asyncpg.Pool`，`min=5 / max=20`；总连接数 = worker × max，需 ≤ PG `max_connections` | `IBMCP_DB_POOL_MAX` |
| 慢查询治理 | 热点查询（设备列表、展开配置）走 Redis 缓存 + 变更失效                                              | `IBMCP_CACHE_TTL`   |
| 请求体限制 | Nginx `client_max_body_size` + FastAPI 中间件                                                       | —                   |
| 输出裁剪   | 大列表统一分页，禁止无 `size` 上限查询（`size ≤ 500`）                                              | Pydantic 校验       |

**② 数据写入层**

| 措施       | 说明                                                                                                           |
|------------|----------------------------------------------------------------------------------------------------------------|
| 批量 COPY  | `time_series_data` 用 `COPY ... FROM STDIN` 二进制/文本流，单批 ≤ 2000 行；吞吐较逐条 INSERT 提升 1~2 个数量级 |
| 报警内存态 | 每 point 维护 `{active_id, last_value, state}`，仅在越限状态翻转时打 DB；不再每点一次 `SELECT`                 |
| 写放大消除 | 去掉现有 `checkAndFireAlarm` 中的 `using_points` 查询，改为阈值随 plan 下发，Python 侧内存态判定               |
| 分区友好   | 批量 COPY 按 `ts` 顺序写入，PostgreSQL chunk 命中率高，避免随机 chunk 切换                                     |

> **关键收益点**：现有实现单点 3 次 DB 往返（INSERT + SELECT points + SELECT active alarm）；重构后单点 **0 次独立往返**（攒批
> COPY + 状态翻转时才 upsert）。500 点/秒场景下 DB 往返从 ~1500 次/秒 降至个位数批次/秒。

**③ 采集扩展层**

| 措施                 | 说明                                                                                                    |
|----------------------|---------------------------------------------------------------------------------------------------------|
| 线程池替代每设备线程 | 500 台设备从 500 线程降至 `CPU×2` 线程；内存与上下文切换大幅下降                                        |
| 批量读合并           | 保持现有 `readPoints` 合并逻辑（Modbus 相邻寄存器合并为一次 PDU），并对同设备不同点位按 `cycle_ms` 分组 |
| 连接复用             | 每 Plan 复用连接；仅在连续 N 次失败后才断开重建（现有实现"全失败即断连"过于激进）                       |
| 时间轮               | O(1) 到期判断，避免每轮扫全部点位（现有 `collectDevice` 每轮遍历所有点）                                |
| 无锁事件通道         | 采样具体走无锁快照环形缓冲（Python 定时拉取），状态事件走独立队列                                       |
| 背压                 | 当事件队列深度 > 阈值时，扩展丢弃最旧批次并计数（`dropped_batches`），绝不阻塞采集线程                  |

### 8.3 与现有实现的并发指标对照（设计目标）

| 指标             | 历史方案实现               | FastAPI + C++ 采集扩展 目标                            |
|------------------|----------------------------|--------------------------------------------------------|
| 采集线程数       | = 设备数                   | `CPU×2`（可配）                                        |
| 时序写入往返     | 1/点                       | 1/批（500~2000 点/批）                                 |
| 报警判定 DB 往返 | 2/点                       | 0（状态翻转时 1）                                      |
| 配置生效延迟     | 最长 5s（轮询）            | < 100ms（推送 + ack）                                  |
| 单设备故障影响面 | 进程内驱动异常可致全站崩溃 | 驱动层 try/catch + 超时隔离；进程级由 systemd 重启兜底 |
| API 与采集耦合   | 同进程                     | 同进程（扩展形态），部署仅一个 unit                    |
| 调用开销         | 进程内虚函数调用           | 进程内 pybind11 调用（µs 级，无 JSON 编解码）          |

---

## 九、openEuler 生产部署（含 PG15 / Redis / Nginx）

### 9.1 目标拓扑（单机起步，预留横向扩展）

```
┌──────────── openEuler 服务器（x86_64 / ARM64 均可） ─────────────┐
│                                                                 │
│  Nginx :443 ──► uvicorn :8000 (单进程单 worker) ──┬─► PG15 :5432       │
│    │                                        ├─► Redis :6379       │
│    └─ 静态资源 (Vue 构建产物 dist/)           └─► 进程内 industrial_collector 扩展 │
│                                                                 │
│  单一 systemd unit：ibmcp-api（FastAPI + 采集扩展同进程）          │
└─────────────────────────────────────────────────────────────────┘
```

### 9.2 组件与选型

| 组件       | 版本/包（openEuler）                                                                                                                                     | 用途                                       | 备注                                                                     |
|------------|----------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------|--------------------------------------------------------------------------|
| openEuler  | 24.03 LTS（推荐）                                                                                                                                        | 基础 OS                                    | 也支持 22.03 LTS                                                         |
| Python     | 3.11+（`dnf install python3.11 python3.11-devel`）                                                                                                       | FastAPI 运行时                             | 若系统自带版本偏低，用 `pyenv` 或 `uv python install 3.11`               |
| PostgreSQL | 18（PGDG 官方 repo）                                                                                                                                     | 业务库                                     | 见 9.3 安装顺序                                                          |
| PostgreSQL | `postgresql-2-postgresql-18`                                                                                                                             | 时序普通表                                 | **必须与 PG15 ABI 匹配**；若不支持，则时序查询降级为分区表方案（见 9.6） |
| Redis      | 7.x（`dnf install redis`）                                                                                                                               | 缓存 / WS 订阅状态 / 限流                  | `appendonly yes`                                                         |
| Nginx      | 1.24+（`dnf install nginx`）                                                                                                                             | 反代 / TLS / 静态                          | WebSocket 需 `proxy_http_version 1.1`                                    |
| 编译工具链 | `gcc 12+`、`cmake 3.20+`、`python3.11-devel`、`libmodbus-devel`、`libmosquitto-devel`、`open62541`（源码编）、`pip`（含 `scikit-build-core`/`pybind11`） | C++ 采集扩展编译                           | 只编 Linux 产物（Python 小版本须与运行机一致）                           |
| 进程托管   | systemd                                                                                                                                                  | **单个 unit**：`ibmcp-api`（库随进程加载） | 见 9.5                                                                   |

### 9.3 安装顺序（欧拉侧）

```bash
# 0) 基础
dnf install -y gcc gcc-c++ make cmake git python3.11 python3.11-devel nginx redis

# 1) PostgreSQL 15（PGDG 官方源，非系统自带源）
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
dnf install -y postgresql18-server postgresql18-contrib
/usr/pgsql-18/bin/postgresql-18-setup initdb
systemctl enable --now postgresql
# 启用扩展所需参数后重启
sed -i "s/shared_preload_libraries =.*/shared_preload_libraries = 'postgresql'/" /var/lib/pgsql/18/data/postgresql.conf
systemctl restart postgresql-18

# 2) PostgreSQL（与 PG15 对应版本）
dnf install -y postgresql-2-postgresql-18
postgresql-tune --yes

# 3) Redis
systemctl enable --now redis
# /etc/redis/redis.conf: appendonly yes, maxmemory-policy allkeys-lru

# 4) Nginx
systemctl enable --now nginx

# 5) 协议栈（C++ 采集扩展依赖）
dnf install -y libmodbus-devel libmosquitto-devel
# open62541 需源码编译（版本 1.4.x）并 make install，供 CMake find_package 使用
# pybind11 / scikit-build-core 由 pip 在 PEP 517 隔离环境内自动安装，无需系统包
```

> **注意**：欧拉各版本的系统默认源里 PostgreSQL 可能是 13/14/15，**必须使用 PGDG 官方源安装 PG15**，否则 PostgreSQL
> 版本不匹配。若内网无外网，需提前离线下载 RPM 及其依赖。

### 9.4 数据库初始化与演进

- **首次部署**：以现有 [db/01-init.sql](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/backend/db/01-init.sql)
  为基线执行一次（DDL 全幂等），再执行 `../../backend/migrations` 中的增量脚本（新增 `runtime_plan_revision`、
  `audit_config_change`、`using_device_runtime`）。
- **后续演进**：统一走 Alembic（`alembic upgrade head`），不再手改 SQL。
- **单版本演进**：FastAPI 版是唯一在跑的后端，表结构以 Alembic 基线为准，不再保留旧版后端的兼容分支。

### 9.5 systemd 单元（唯一一个 unit）

```ini
# /etc/systemd/system/ibmcp-api.service
[Unit]
Description=IBMCP FastAPI + collection extension
After=network.target postgresql-18.service redis.service

[Service]
Type=simple
User=ibmcp
WorkingDirectory=/opt/ibmcp
EnvironmentFile=/opt/ibmcp/.env
# 关键：协议库（libmodbus/open62541/libmosquitto）路径写死，避免依赖用户环境变量
Environment=LD_LIBRARY_PATH=/usr/local/lib
ExecStart=/opt/ibmcp/.venv/bin/uvicorn app.main:app \
  --host 127.0.0.1 --port 8000 --workers 1 --loop uvloop
Restart=always
RestartSec=3
LimitNOFILE=1048576
# 采集扩展内存泄漏排查用；正常运行时留空
Environment=MALLOC_ARENA_MAX=2

[Install]
WantedBy=multi-user.target
```

> **不设独立采集服务（如 `ibmcp-collector.service`）** —— 采集扩展随 `ibmcp-api` 进程 `import` 加载，没有任何独立采集服务。
>
> **`--workers 1` 是刻意的**：单 unit 单进程同时承载 REST 与 `/ws/collect`，采集扩展在本进程内被 `import` 一次；多 worker
> 会让每个子进程各自加载一份扩展、各自采集一份，并让 WS 广播只能触达连在同一进程上的前端。扩容手段是
> **升单机资源或分实例部署**，不是加 worker（见 3.3 与 03 号 2.6）。
>
> **禁用 `--preload`**（见 3.3 fork 说明）。若必须用 gunicorn，请写成
> `gunicorn -k uvicorn.workers.UvicornWorker -w 1 app.main:app`（**不要**加 `--preload`）。

### 9.6 Nginx 配置要点

```nginx
upstream ibmcp_api { server 127.0.0.1:8000; keepalive 64; }

server {
    listen 443 ssl;
    ssl_certificate     /etc/nginx/certs/ibmcp.crt;
    ssl_certificate_key /etc/nginx/certs/ibmcp.key;

    root /opt/ibmcp/web;                 # Vue 构建产物
    location / { try_files $uri /index.html; }

    location /api/ {
        proxy_pass http://ibmcp_api;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Request-Id $request_id;
        proxy_read_timeout 60s;
    }

    location /ws/collect {                # WebSocket 必须显式升级（四固定主题：data/device/alarm/control）
        proxy_pass http://ibmcp_api;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 3600s;        # 长连接，配合服务端心跳
    }
}
```

> **注意**：uvicorn 默认不支持多 worker 的 WebSocket 广播跨进程（每个 worker 有自己的连接表）。**WebSocket 必须走单 worker
> 的独立进程**，或用 Redis Pub/Sub 做跨 worker 分发。推荐做法：
> - 拆分为 `ibmcp-api`（多 worker，纯 REST）+ `ibmcp-ws`（单 worker，仅 WS，订阅 Redis 频道）
> - 采集事件写入 Redis `channel:ibmcp:events`，WS 进程订阅后广播
>
> 这是多 worker 架构下的必要调整（单进程内 `subscriptions_` 天然共享，多 worker 后跨进程不再成立，必须靠 Redis Pub/Sub 分发）。

### 9.7 容量与调优基线

| 参数                      | 建议值        | 说明                                                                |
|---------------------------|---------------|---------------------------------------------------------------------|
| `IBMCP_API_WORKERS`       | `min(CPU, 8)` | 超过 8 收益递减，CPU 主要耗在 JSON 序列化                           |
| `IBMCP_DB_POOL_MAX`       | 20            | 保证 `workers × 20 ≤ PG max_connections × 0.6`                      |
| `IBMCP_KERNEL_IO_THREADS` | `CPU × 2`     | 设备多、协议慢（OPC UA 建链慢）时调大                               |
| `IBMCP_FLUSH_INTERVAL_MS` | 200           | 越小实时性越好、写入次数越多；200ms 在 5000 点/秒下仍为 5 次/秒写批 |
| `IBMCP_EVENT_QUEUE_MAX`   | 4096          | 队列满触发库背压丢弃并计数                                          |
| PG `work_mem`             | 32MB          | 聚合视图查询                                                        |
| PG `shared_buffers`       | 内存 25%      | `postgresql-tune` 自动调                                            |
| PostgreSQL chunk          | 1 day         | 保持现有策略；点位 > 5000 时改 12 小时                              |

### 9.8 部署检查清单（扩展形态专项）

上线前逐项打勾，以下任一项不满足都可能导致"扩展导入失败"或"采集中断且无日志"：

| #  | 检查项                                           | 验证命令 / 方法                                                                |
|----|--------------------------------------------------|--------------------------------------------------------------------------------|
| 1  | 扩展 `.so` 的协议库依赖可解析（无 `not found`）  | `ldd $(python -c "import industrial_collector._core as m; print(m.__file__)")` |
| 2  | 扩展可正常 import 且读出版本                     | `python -c "from industrial_collector import _core; print(_core.health())"`    |
| 3  | 构建 Python 小版本 == 运行 Python 小版本         | `python -c "import sys; print(sys.version_info[:2])"` 与构建日志比对           |
| 4  | ABI 版本与 `IBMCP_KERNEL_ABI_EXPECT` 一致        | 启动日志校验 `health()` 输出                                                   |
| 5  | **未使用 `gunicorn --preload`**                  | `systemctl cat ibmcp-api` 检查 `ExecStart`                                     |
| 6  | `Collector()` 构造在 lifespan 内（fork 之后）    | 代码走查 `app/main.py`                                                         |
| 7  | 事件回调对象存活于 lifespan 对象上（非局部变量） | 代码走查 `collector_orchestrator.py`                                           |
| 8  | `systemd` 已配 `Restart=always`                  | `systemctl show ibmcp-api -p Restart`                                          |
| 9  | 重启后 5s 内 `plan_ack` 成功                     | `journalctl -u ibmcp-api \| grep plan_ack`                                     |
| 10 | 崩溃后无残留 OpenPG/Modbus 半开连接（设备侧）    | 现场设备连接数观察                                                             |

---

## 十、Windows 11 开发 + openEuler 运行调试

> 用户明确：**开发在 Windows 11，运行与调试都在欧拉系统**。这与现有"Windows 编辑 + Debian
> 远程构建"模式一致（见 [02.01 文档 2.10 节 CLion 远程开发](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/01-docs/开发指南/02.01-%20Cpp_Drongon_软件代码架构说明.md)）。

### 10.1 开发模式总览

| 环节             | Windows 11（开发机）                                                                                           | openEuler（目标机）                         |
|------------------|----------------------------------------------------------------------------------------------------------------|---------------------------------------------|
| 代码编辑         | VS Code / PyCharm（Python）+ CLion（C++）                                                                      | —                                           |
| Python 依赖解析  | 可选本地 venv（仅用于 lint/类型检查）                                                                          | 生产 venv（实际运行）                       |
| C++ 采集扩展编译 | **不本地编译**（协议库为 Linux 生态，libmodbus/open62541 交叉编译成本高；且 pybind11 扩展与 CPython ABI 绑定） | `pip install -e ./backend` 远程构建         |
| 运行 / 调试      | 通过 SSH 远程                                                                                                  | `uvicorn --reload`（开发）/ systemd（生产） |
| 数据库           | 远程连接欧拉 PG15                                                                                              | 本地实例                                    |
| 前端             | 本地 `npm run dev` + 代理到欧拉 API                                                                            | Vue 构建产物由 Nginx 服务                   |

**为什么 C++ 不本地编译**：libmodbus / open62541 / libmosquitto 在 Windows 下要么无官方包、要么 ABI 不同；即便勉强编出
`.pyd`，与欧拉上的 `.so` 行为也有差异（串口 RS485、epoll），会导致"本地能跑、现场不能跑"。因此**采集扩展只产出 Linux 产物，Windows
侧不参与 C++ 构建**。

**这带来一个直接后果**：Windows 上无法真正 `import industrial_collector._core`。所有采集相关验证必须在欧拉侧完成（见
10.3），Windows 侧只做 Python 业务逻辑的 lint / 单测（用 stub 桩实现替换，见 10.7）。

### 10.2 工具链配置

**① C++ 侧（CLion 远程工具链）**

1. `Settings → Build, Execution, Deployment → Toolchains → Remote Host`，SSH 指向欧拉服务器。
2. CMake Profile 指定远程编译器（`gcc-12`/`gcc-13`）、构建目录 `/opt/ibmcp/build`、构建类型 `RelWithDebInfo`。
3. Deployment 映射：本地 `../../collector-cpp` ↔ 远程 `/opt/ibmcp/backend/collector-cpp/`，保存自动同步。
4. 远程调试：`gdbserver :12345 /opt/ibmcp/.venv/bin/python -m uvicorn app.main:app` + CLion/VS Code Remote Debug 配置。
   **调试采集扩展必须附着到承载它的 Python 进程**（扩展无独立进程可 attach）。

**② Python 侧（VS Code / PyCharm）**

- 推荐 **Remote-SSH** 直接连欧拉，在服务器上打开 `../../backend`，解释器选 `/opt/ibmcp/.venv/bin/python`。
- 这样 **Pylance/ruff/mypy 的类型解析基于真实依赖版本**，不会出现"Windows 上装不上 psycopg 依赖导致误报警告"。
- 调试用 `uvicorn --reload --reload-dir app` 在服务器启动，VS Code 通过 `debugpy` 附加（`--port 5678`）。

**③ 编辑器统一（`.editorconfig` + `ruff` + `black`）**

- 行尾统一 `LF`（避免 CRLF 进 Linux 造成 systemd `ExecStart` 脚本报错）
- `ruff format` 与 `ruff check` 在 pre-commit 钩子里执行（Windows 与欧拉一致）

### 10.3 常用开发命令（在欧拉服务器执行）

```bash
# 一次性准备
cd /opt/ibmcp
uv venv --python 3.11 .venv
uv pip install -r backend/requirements.txt -r backend/requirements-dev.txt

# C++ 采集扩展构建 + 安装（协议栈见 9.3，scikit-build-core 自动调 CMake）
uv pip install -e ./backend \
      -Ccmake.define.IBMCP_ENABLE_MODBUS=ON \
      -Ccmake.define.IBMCP_ENABLE_MQTT=ON \
      -Ccmake.define.IBMCP_ENABLE_OPCUA=ON

# 验证扩展可被导入（应打印 abi、lib 版本与协议清单）
.venv/bin/python -c "from industrial_collector import _core; print(_core.health())"

# FastAPI 开发运行（扩展随进程 import，无需额外服务）
.venv/bin/uvicorn app.main:app --reload --host 0.0.0.0 --port 8000

# 数据库迁移
.venv/bin/alembic upgrade head

# 测试（Python 业务层可用 stub 桩实现，无需真实扩展）
.venv/bin/pytest -q backend/tests
```

> **`pip install -e` 与增量编译**：改动 C++ 后需重新执行 `uv pip install -e ./backend --no-build-isolation` 才会重新编译（
> `-e` 只对纯 Python 热生效）。**替换扩展 `.so` 必须重启 `ibmcp-api`**（见 10.6）。

### 10.4 前后端联调（Windows 视角）

```powershell
# Windows 侧：Vue 本地开发，API 走 SSH 隧道或直连欧拉
npm run dev -- --proxy http://<欧拉IP>:8000     # 或 vue.config/ vite server.proxy

# 若欧拉仅开放内网/需 SSH 隧道，Windows 上：
ssh -L 8000:127.0.0.1:8000 -L 5432:127.0.0.1:5432 ibmcp@<欧拉IP>
# 之后本地 http://127.0.0.1:8000 与 PG 直连均可用
```

**WebSocket 联调注意**：本地 Vue dev server 代理 WS 需设置 `ws: true`（Vite）或 `ws: true`（http-proxy-middleware），指向
`/ws/collect`；否则 WS 升级请求会退回 HTTP 轮询并失败。

### 10.5 环境变量清单（`.env`，欧拉侧）

```ini
# 基础
IBMCP_ENV=production|development
IBMCP_LOG_LEVEL=INFO
IBMCP_CORS_ORIGINS=http://localhost:5173,https://ibmcp.example.com

# 数据库 / 缓存
IBMCP_DB_DSN=postgresql://ibmcp_app:******@127.0.0.1:5432/device_db
IBMCP_DB_POOL_MIN=5
IBMCP_DB_POOL_MAX=20
IBMCP_REDIS_URL=redis://127.0.0.1:6379/0

# 安全
IBMCP_JWT_SECRET=******
IBMCP_JWT_EXPIRE_SECONDS=604800

# 采集扩展（industrial_collector，进程内 import 加载）
# 扩展已由 pip 安装进 venv site-packages；此处只需保证协议库可解析
IBMCP_KERNEL_ABI_EXPECT=1.2
IBMCP_KERNEL_IMPL=ffi                  # ffi = 真实扩展；stub = 桩实现（Windows/无设备）
IBMCP_KERNEL_IO_THREADS=8
IBMCP_FLUSH_INTERVAL_MS=200
IBMCP_EVENT_QUEUE_MAX=4096
# 协议库（libmodbus/open62541/libmosquitto）安装前缀
LD_LIBRARY_PATH=/usr/local/lib

# API
IBMCP_API_WORKERS=4
IBMCP_PAGE_SIZE_MAX=500
```

> **安全约束**：`.env` 不入库；`IBMCP_JWT_SECRET` 必须由部署脚本生成（`openssl rand -hex 32`），不得沿用现有 `Consts.h` 中的
> `"ibmcp-change-me-in-production"`。

### 10.6 从 Windows 到欧拉的交付流水

```
Windows（编辑 / 本地 lint / 单测 mock）
   │ git push
   ▼
欧拉 CI 或开发服务器（git pull）
   ├─ ruff / mypy / pytest（Python，采集路径用 stub 桩实现）
   ├─ uv pip install -e ./backend（scikit-build-core 编译 _core）+ googletest 驱动测试
   ├─ alembic upgrade head（DB）
   └─ systemctl restart ibmcp-api        # 采集扩展随进程重载生效
   ▼
欧拉运行环境（生产）
```

> **注意**：**替换扩展 `.so` 必须重启 `ibmcp-api`** —— 运行中的进程已把旧扩展映射进地址空间，热替换文件不会让新扩展生效（且
> Linux 下旧 inode 仍被占用）。因此扩展升级走"停服务 → 装扩展 → 起服务"的原子窗口。

### 10.7 开发期 stub 桩实现（Windows 可用）

为了让 Windows 上也能跑通业务流程（不依赖 `industrial_collector._core`），`app/core/collector/` 同时提供两个实现，由
Settings 的 `collector_backend` 选择：

| 实现     | 文件                                                         | 场景                          |
|----------|--------------------------------------------------------------|-------------------------------|
| 真实扩展 | `app/core/collector/ffi.py`（薄封装 `industrial_collector`） | 欧拉侧运行 / 调试             |
| 桩实现   | `app/core/collector/stub.py`                                 | Windows 单测 / 前端联调无设备 |

```python
# Settings 中：collector_backend = "ffi" | "stub"（环境变量 IBMCP_COLLECTOR_BACKEND）
# stub.py 实现与 05 号 5.3 完全相同的 CollectorApi 接口
#   （init / apply_plan / set_event_callback / snapshot_since / write_point
#     / list_protocols / health / shutdown）
# 并按 plan 中的 cycle_ms 定时合成读数写入快照，可模拟越限、离线、写入失败；
# 校验逻辑（4001~4006）与扩展保持一致，保证契约测试有效
```

> **stub 不是"随便返回假数据"**：它必须复刻 `apply_plan` 的全部校验规则（协议白名单、`scale=0`、`address` 为空、点位重复），否则
> Windows 上跑通的用例到欧拉会直接 `4002`。详见 05 号 5.8。

**收益**：前端与业务逻辑开发完全不必等硬件与欧拉环境；契约测试（S0 阶段）可直接用 stub 驱动，后续换成真扩展时业务层零改动。

---

## 十一、模块映射与分期落地路线

> 原 Drogon 后端已停止维护，不存在并行灰度与回退。下表列出历史方案各模块在 FastAPI + C++ 扩展架构中的对应去向，作为从零构建时的模块划分参考。

### 11.1 模块去向一览

| 历史方案模块                                                                              | 去向                                                                  | 说明                                            |
|-------------------------------------------------------------------------------------------|-----------------------------------------------------------------------|-------------------------------------------------|
| `controllers/*`（12 个 HTTP 控制器）                                                      | **→ FastAPI `api/v1/*`**                                              | 全部重写为 Python 路由，响应体语义保持一致      |
| `services/DictService` / `BaseDeviceService` / `ProcessRouteService` / `EquipmentService` | **→ FastAPI `services/static/*`**                                     | 对齐 01 号字典                                  |
| `services/EquipmentConfigService` / `RouteBindService`                                    | **→ FastAPI `services/running/*` + `plan_compiler`**                  | 新增 plan 编译能力                              |
| `services/CollectorService`                                                               | **→ collector-cpp `scheduler/` + FastAPI `collector_orchestrator`**   | 拆成"扩展内调度"与"下发编排"两半                |
| `services/DataService`                                                                    | **→ FastAPI `ingest_service` / `query_service`**                      | 改为批量 COPY                                   |
| `services/AlarmService`                                                                   | **→ FastAPI `alarm_engine`**                                          | 内存态判定 + 批量 upsert                        |
| `services/ControlService`                                                                 | **→ FastAPI `control_service` + 扩展 `write_point`**                  | 台账在 Python，执行在扩展                       |
| `services/AuthService` + `CryptoUtil`                                                     | **→ FastAPI `core/security.py`**                                      | 统一使用 argon2 哈希（无旧版兼容负担）          |
| `controllers/WebSocketController`                                                         | **→ FastAPI `ws/hub.py`（`/ws/collect` 四主题；必要时独立 WS 进程）** | 主题保持 `data/device/alarm/control`            |
| `drivers/*`（7 个驱动）                                                                   | **→ collector-cpp `drivers/*`**                                       | 尽量原样迁移，去掉 `DbHelper` / WS 依赖         |
| `common/DbHelper` / `common/Result.h`                                                     | **废弃**                                                              | 扩展不碰 DB；Result 由 Python 中间件实现        |
| `db/01-init.sql`                                                                          | **→ 基线 + Alembic 迁移**                                             | 触发器同步链取消（见 5.3）                      |
| `main.cpp`                                                                                | **→ `app/main.py`**                                                   | `/api/ping`、`/api/collector/status` 保留同路径 |

### 11.2 分期落地路线（每阶段可独立上线验收）

| 阶段                     | 内容                                                                                                                                                     | 验收标准                                                                            | 风险控制                                  |
|--------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------|-------------------------------------------|
| **P0 契约先行**          | 按 05 号写 `../../pyproject.toml` + `collector-cpp/CMakeLists.txt` + `python/industrial_collector/` 骨架（空 `_core` 仅 `health()`）；同步落实 `stub.py` | FastAPI 全链路可跑通（下发 + 快照拉取 + 事件消费），不依赖真实设备                  | stub 与真扩展同接口同校验，后续替换零改动 |
| **P1 静态+运行动态配置** | 01/02 号文档全部表的 FastAPI CRUD + 校验 + 级联展开 + `/{id}/plan` 预览                                                                                  | 与前端联调通过（响应体符合 03 号契约）                                              | stub 驱动全链路跑通，不依赖真实设备       |
| **P2 采集扩展重构**      | 驱动迁入 `../../collector-cpp`，实现时间轮 + IO 线程池 + 无锁快照缓冲 + `apply_plan`                                                                     | 单机 300 台模拟设备（可写 Modbus slave 模拟器）稳定采集 24h，无线程暴涨、无内存增长 | 用模拟器压测，不接生产设备                |
| **P3 写入与报警优化**    | 批量 COPY、内存态报警、WS 独立进程 + Redis 广播                                                                                                          | 5000 点/秒持续压测，PG CPU < 40%，端到端延迟 p99 < 1s                               | 压测环境独立库，不影响生产                |
| **P4 部署上线**          | openEuler systemd + Nginx + systemd unit + Alembic 基线                                                                                                  | 生产环境全量切流至 FastAPI 版，运行稳定                                             | 上线前完成全量回归测试                    |

### 11.3 数据库初始化注意事项

FastAPI 版是唯一在跑的后端，数据库初始化时：

1. **以 Alembic 基线为准**：执行 `alembic upgrade head` 一次性建齐全部表，不再依赖旧版 SQL 脚本。
2. `time_series_data` 主键 `(ts, point_id)` 加 `ON CONFLICT DO NOTHING`，防止重复采样写入报错。
3. `using_devices` / `using_points` 由扩展快照写入，FastAPI 不直接写这两张表。

### 11.4 待决问题（需业务方确认）

| 编号 | 问题                                                                                         | 建议                                                                    |
|------|----------------------------------------------------------------------------------------------|-------------------------------------------------------------------------|
| Q1   | PostgreSQL 是否已发布 PG15 适配版本？若否，是否接受"时序表按月分区 + 应用层聚合"替代普通表？ | 优先确认版本可用性，这是 P3 的前置条件                                  |
| Q2   | 现场最大设备数与最大点位总数？（决定 `IBMCP_KERNEL_IO_THREADS` 与 worker 数配置）            | 建议目标 ≤ 500 设备 / ≤ 20000 点位                                      |
| Q3   | 桌面端 / Android 端是否直连后端（不经 Nginx）？                                              | 是。Tauri 桌面端与 Android 端直连后端 8000 端口（内网），详见 07 号文档 |
| Q4   | 控制写入是否需要同步等待结果（`wait=true`）？                                                | 默认异步；关键控制点由前端显式指定                                      |
| Q5   | 扩展形态下若协议栈导致进程崩溃，是否接受 3~5 秒采集中断 + 重启恢复？                         | 建议接受（无状态重启 + 配置重放）；若不可接受，需重新评估进程隔离方案   |
| Q6   | 历史数据保留 90 天的策略是否调整？                                                           | 维持现状，后续按容量评估                                                |
| Q7   | 采集扩展以 wheel 打包分发，还是独立按 `/usr/local/lib` 安装？                                | 长期建议 wheel 化，与 Python 应用版本强绑定                             |

---

## 十二、附录

### 12.1 关键时刻的性能预算（设计目标，非实测）

| 环节                  | 预算                        | 说明                             |
|-----------------------|-----------------------------|----------------------------------|
| REST 配置类接口       | p99 < 50ms                  | 无设备 IO，纯 DB + 校验          |
| 计划下发 `apply_plan` | p99 < 20ms                  | 只做校验与注册，不建链           |
| 事件 → 落库           | p99 < 300ms                 | 含 200ms 攒批窗口                |
| 事件 → 前端 WS 可见   | p99 < 500ms                 | 落库与广播并行（广播不等待落库） |
| 控制下发受理          | p99 < 30ms                  | 入队即返回                       |
| 控制执行回执          | 取决于设备（典型 50~500ms） | 由 `write_result` 事件送达       |

### 12.2 术语对照

| 术语                       | 含义                                                                                                     |
|----------------------------|----------------------------------------------------------------------------------------------------------|
| Plan（采集计划）           | `running_*` 配置编译后、下发给采集扩展的设备+点表结构，见 7.2                                            |
| Revision（计划版本）       | 每次下发的递增编号，用于幂等与回滚                                                                       |
| 采集扩展                   | `industrial_collector._core`，pybind11 扩展模块，被 FastAPI 进程 `import` 调用，见 3.2 D1 / 7.1 与 05 号 |
| Event Callback             | 扩展在自身 C++ 线程调用 Python 回调传回状态事件，见 7.3                                                  |
| 采样快照（snapshot_since） | 扩展内无锁环形缓冲，Python 每 200ms 拉取增量，见 05 号 3.3/5.5                                           |
| 快照表                     | `using_devices` / `using_points` / `using_device_runtime`，存储运行态，非配置真相源                      |
| 真相源                     | `base_*`（静态模板）+ `running_*`（运行配置），见 5.3                                                    |
| stub 桩实现                | `app/core/collector/stub.py`，与真扩展同接口同校验的桩实现，供 Windows 单测，见 10.7 与 05 号 5.8        |

### 12.3 相关文档索引

-
静态字段：[01-静态数据表字段定义.md](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/01-docs/数据字典/01-静态数据表字段定义.md)
-
运行动态配置：[02-运行动态配置表字段定义.md](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/01-docs/数据字典/02-运行动态配置表字段定义.md)
-
运行期核心与系统表：[03-运行期核心与系统管理表字段定义.md](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/01-docs/数据字典/03-运行期核心与系统管理表字段定义.md)
- 现有 Drogon
  后端架构：[02.01- Cpp_Drongon_软件代码架构说明.md](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/01-docs/开发指南/02.01-%20Cpp_Drongon_软件代码架构说明.md)
- API
  与测试用例：[03-API接口及测试用例-涵盖数据.md](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/01-IBMCP/01-docs/开发指南/03-API接口及测试用例-涵盖数据.md)