# IBMCP 数据模型与字段契约（FastAPI 版）

>
版本：v1.0　　编写日期：2026-11-18　　目标运行环境：openEuler　　配套主文档：[01-IBMCP-FastAPI重构架构设计.md](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/01-IBMCP-FastAPI重构架构设计.md)
>
> **本文档定位**：把分散在 `01-docs/数据字典` 与 `01-docs/开发指南` 中的**关键字段**与**关键约束**，按 FastAPI
> 重构后的职责分工重新组织为**可直接落地建表 / 建 Schema / 建校验规则**的单一参考。
> 本文档不重复架构叙述，只回答三件事：
> 1. 这张表**归谁管**（FastAPI 真相源 / 扩展设备计划 / 运行态快照）；
> 2. 这张表的**关键字段与约束**是什么（类型、可空、唯一、外键、默认值）；
> 3. 落到 FastAPI 上**对应什么 Schema / 校验规则 / 路由**。
>
> **权威顺序**：本文档与 `数据字典` 原始文档冲突时，以本文档为准（因重构后 `using_*` 语义已变，见第二章）。原始文档保留作为历史基线。

---

## 一、表分区：重构后的三类归属

重构最大的结构变化是**取消了"数据库触发器把配置态同步到运行态"这条链**（原 `trg_running_config_sync` /
`sync_config_to_using_tables()`，见开发指南 02.03 号文档）。替代方案是 **配置 → 计划 → 扩展** 的单向编译与下发。

因此全部表在 FastAPI 版中被划分为三类：

| 类别                  | 前缀                                                                   | 归属                     | 是否可写            | 说明                                                                          |
|-----------------------|------------------------------------------------------------------------|--------------------------|---------------------|-------------------------------------------------------------------------------|
| **A 静态字典/模板**   | `base_*`（10 张）                                                      | FastAPI 真相源           | FastAPI 全权 CRUD   | 配置向导的"选项数据源"，跨项目复用的主数据                                    |
| **B 运行动态配置**    | `running_*`（5 张）                                                    | FastAPI 真相源           | FastAPI 全权 CRUD   | 装备实例化结果；**编译成采集计划 plan 的唯一来源**                            |
| **C 运行期结果/快照** | `using_*`、`time_series_data`、`using_alarms`、`control_commands`      | FastAPI 写入（业务结果） | 仅 FastAPI 业务侧写 | **不再由触发器维护**；`using_devices` / `using_points` 降级为"扩展运行态快照" |
| **D 系统管理**        | `users`、`roles`、`user_roles`                                         | FastAPI 真相源           | FastAPI 全权 CRUD   | RBAC，密码改 argon2，`users` 补 `roles[]` 反查能力                            |
| **E 重构新增**        | `runtime_plan_revision`、`audit_config_change`、`using_device_runtime` | FastAPI                  | FastAPI 写          | 见主文档 5.4 / 6.3，本文档第八章给出字段                                      |

**一句话记法**：

```
base_*    = 模板（怎么配）
running_* = 配置（这台装备现在怎么配）  ← 唯一真相源，编译成 plan
using_*   = 快照（扩展现在真正在采什么） ← 只读展示，不回写配置
```

---

## 二、关键语义变更（必须先看，否则会照旧表设计写错代码）

| #  | 变更点                        | 旧（历史方案）                                                  | 新（FastAPI 版）                                                                                                                         | 影响的表                        |
|----|-------------------------------|-----------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------|---------------------------------|
| S1 | 配置生效链路                  | DB 触发器 `running_* → using_*` 自动同步                        | FastAPI `plan_compiler` 编译 → `collector.apply_plan` → `plan_ack`                                                                       | 全部 `running_*` / `using_*`    |
| S2 | `using_*` 表角色              | 与 `running_*` 双写、可被触发器覆盖                             | **只做扩展运行态快照**，由 FastAPI 在 `plan_ack` 后写一次，不再被配置覆盖                                                                | `using_devices`、`using_points` |
| S3 | `using_devices.status` 写入方 | 触发器 INSERT 置 0 + CollectorService 回写                      | **仅扩展的 `device_status` 事件**决定，FastAPI 消费事件后写 `using_device_runtime` 并同步 `using_devices.status`                         | `using_devices`                 |
| S4 | 时序数据写入方式              | 逐点 `INSERT`（`DataService::ingest`）                          | 攒批 `COPY`（`ingest_service`），批 ≤ 2000 行                                                                                            | `time_series_data`              |
| S5 | 报警判定位置                  | 每点写后查 `using_points` 再判定（3 次往返）                    | `alarm_high/low/enabled` 随 **plan** 下发，FastAPI 用 `snapshot_since()` 拿到的样本内存态判定                                            | `using_alarms`、plan            |
| S6 | 点位地址来源                  | `running_device_collection.address_override` → 回退 `item_code` | `address_override` → 回退 `base_device_data_item_dict.address_rule`（**不再回退 item_code**，因 item_code 是业务编码不是通信地址）       | `running_device_collection`     |
| S7 | 采集周期单位                  | `collection_cycle`（秒），未同步到 `scale/offset`               | plan 中统一 `cycle_ms = collection_cycle * 1000`，且 `scale/offset/alarm_*` **全部随 plan 下发**（填补旧版"未同步字段需二次维护"的缺口） | `running_device_collection`     |
| S8 | 密码哈希                      | OpenSSL PBKDF2-HMAC-SHA256                                      | argon2id（保留 PBKDF2 校验分支做平滑迁移）                                                                                               | `users`                         |
| S9 | 唯一键冲突响应                | SQL 异常 → `code=409`                                           | Pydantic 前置校验 + 捕获 `asyncpg.UniqueViolationError` → `code=409`                                                                     | 全部                            |

> **S6 / S7 是最容易踩的两处**。旧版同步函数里 `address = COALESCE(NULLIF(address_override,''), item_code)`
> ，把"数据项编码"当成"通信地址"落到了 `using_points.address`；而 `scale/offset/alarm_*` 根本没同步，全是建表默认值（
> `1.0 / 0.0 / 0.0 / 0.0 / FALSE`），必须靠 `POST /api/config/points` 二次维护。FastAPI 版在 `plan_compiler` 中一次性解决。

---

## 三、A 类：静态字典与模板表（10 张，FastAPI 真相源）

### 3.0 通用约定

- 所有静态表统一提供六件套路由（`list` / `all` / `get` / `create` / `update` / `remove`），由 `StaticDictRouter` 生成，见主文档
  4.1；
- 所有 `*_code` 字段为**业务唯一键**，`VARCHAR(64)`，正则 `^[A-Za-z0-9_-]+$`；
- 所有表统一带 `created_by / created_at / updated_by / updated_at`（除 01 号文档原表未定义者，本文档标注为准）；
- **跨表引用必须在 Pydantic 层校验存在性**（不依赖 DB 外键报错），见 3.11。

### 3.1 `base_device_base_info`（设备核心基础数据）

**关键字段**

| 字段                        | 类型         | 约束                | FastAPI 校验要点                                                       |
|-----------------------------|--------------|---------------------|------------------------------------------------------------------------|
| `id`                        | BIGSERIAL PK | —                   | 只读                                                                   |
| `device_code`               | VARCHAR(64)  | **NOT NULL UNIQUE** | `pattern ^[A-Za-z0-9_-]+$`；`running_route_device` 引用它              |
| `device_name`               | VARCHAR(128) | NOT NULL            | `min_length=1`                                                         |
| `device_category_code`      | VARCHAR(64)  | 可空                | 必须存在于 `base_device_category_dict.category_code`                   |
| `brand_code` / `model_code` | VARCHAR(64)  | 可空                | `(brand_code, model_code)` 组合应存在于 `base_device_brand_model_dict` |
| `serial_number`             | VARCHAR(128) | 可空                | —                                                                      |
| `manufacturer`              | VARCHAR(128) | 可空                | —                                                                      |
| `supported_protocols`       | **JSONB**    | 默认 `[]`           | Schema 声明 `list[str]`，每项必须 ∈ `collector.list_protocols()`       |
| `supported_ports`           | **JSONB**    | 默认 `[]`           | Schema 声明 `list[str]`，每项必须 ∈ `base_device_port_dict.port_code`  |
| `data_items`                | **JSONB**    | 默认 `[]`           | Schema 声明 `list[DataItemRef]`（`item_code` + 可选 `default_cycle`）  |
| `description`               | TEXT         | 可空                | —                                                                      |

**关键约束**：`device_code` 全库唯一，且 **`using_devices.device_code` / `running_route_device.device_code` /
`using_alarms.device_code` 全部以它为业务键锚点**。改名必须走 `ON UPDATE CASCADE`，不允许"删旧建新"。

**FastAPI 落点**：`schemas/static/device_base.py` → `repositories/static/device_repo.py` → `/api/v1/static/devices`。

### 3.2 `base_device_category_dict`（设备类别）

| 字段            | 类型         | 约束                |
|-----------------|--------------|---------------------|
| `category_code` | VARCHAR(64)  | **NOT NULL UNIQUE** |
| `category_name` | VARCHAR(128) | NOT NULL            |
| `description`   | TEXT         | 可空                |

**用途**：`base_device_base_info.device_category_code`、`base_equipment_base_info.category_code`、
`base_equipment_function_dict.category_code`、`base_device_data_item_dict.category_code` 共同的字典源。

### 3.3 `base_device_brand_model_dict`（品牌型号）

| 字段          | 类型         | 约束     | 说明                     |
|---------------|--------------|----------|--------------------------|
| `brand_code`  | VARCHAR(64)  | NOT NULL | 与 `model_code` 组成唯一 |
| `brand_name`  | VARCHAR(128) | NOT NULL | —                        |
| `model_code`  | VARCHAR(64)  | NOT NULL | —                        |
| `model_name`  | VARCHAR(128) | NOT NULL | —                        |
| `status`      | SMALLINT     | 默认 1   | 0=停用，1=启用           |
| `description` | TEXT         | 可空     | —                        |

**唯一约束**：`UNIQUE (brand_code, model_code)`。**注意**：旧版把 `brand_code` 单列当了唯一键，导致一个品牌只能有一个型号 ——
FastAPI 版必须改为组合唯一。

### 3.4 `base_device_port_dict`（端口）

| 字段          | 类型         | 约束                | 说明                                      |
|---------------|--------------|---------------------|-------------------------------------------|
| `port_code`   | VARCHAR(64)  | **NOT NULL UNIQUE** | 如 `RS485` / `RS232` / `ETHERNET` / `USB` |
| `port_name`   | VARCHAR(128) | NOT NULL            | —                                         |
| `port_type`   | VARCHAR(64)  | 可空                | 与 `port_code` 语义可重叠，保留兼容       |
| `description` | TEXT         | 可空                | —                                         |

### 3.5 `base_device_protocol_dict`（协议字典）

| 字段            | 类型         | 约束                | 说明                           |
|-----------------|--------------|---------------------|--------------------------------|
| `protocol_code` | VARCHAR(64)  | **NOT NULL UNIQUE** | 必须与 **扩展可用协议名** 对齐 |
| `protocol_name` | VARCHAR(128) | NOT NULL            | —                              |
| `protocol_type` | VARCHAR(64)  | 可空                | 展示用分类                     |
| `description`   | TEXT         | 可空                | —                              |

**关键约束（本次重构强化）**：`protocol_code` 的合法值 **以 `Collector.list_protocols()` 为准**
，不再是"字典随便加、驱动再说"。启动时 FastAPI 执行一次一致性校验：

```
extension_protocols = collector.list_protocols()     # ["modbus_tcp","modbus_rtu","mqtt","opc_ua","socket_tcp","socket_udp","web_socket"]
db_protocols        = SELECT protocol_code FROM base_device_protocol_dict
drift               = db_protocols - extension_protocols
→ drift 非空则 WARN 记录到日志（不阻断启动，但配置向导前端应置灰不可选）
```

**当前 11 项字典值（来自 `01-init.sql`）**：`MODBUS_TCP` / `MODBUS_RTU` / `OPC_UA` / `MQTT` / `PROFINET` / `PROFIBUS_DP` /
`CCLINK` / `S7_COMM` / `SOCKET_TCP` / `SOCKET_UDP` / `WEBSOCKET`。

> **有 4 项无驱动**（`PROFINET` / `PROFIBUS_DP` / `CCLINK` / `S7_COMM`）属**预留字典项**，FastAPI 版应新增字段
> `has_driver BOOLEAN`（由启动校验回填）供前端置灰，避免用户选了却采不到。
> **`OPC_DA` 已永久移除**（OPC Classic 依赖 Windows DCOM，Linux/openEuler 无法实现）。

**协议名归一化**（沿用现有 `DriverFactory::normalize()` 语义，Python 侧 `plan_compiler` 必须同实现）：

```python
def normalize(name: str) -> str:
    # 丢弃 _ - 空格 . ，统一小写
    return "".join(c for c in name.lower() if c not in "_- .")
# MODBUS_TCP / Modbus TCP / modbus-tcp / modbus.tcp → "modbustcp"
```

> 归一化只用于**比对**，plan 中下发的 `protocol` 仍用**小写下划线规范名**（`modbus_tcp`
> ）。两套命名不能混用，否则会出现"字典显示可用、扩展建不出驱动"的假可用。

### 3.6 `base_device_data_item_dict`（开放数据项字典）★关键

这是**采集计划的点位来源**，字段质量直接决定 plan 是否可编译。

| 字段                      | 类型         | 约束                   | plan 映射                                                              |
|---------------------------|--------------|------------------------|------------------------------------------------------------------------|
| `item_code`               | VARCHAR(64)  | **NOT NULL UNIQUE**    | 仅业务编码，**不作通信地址**（见变更 S6）                              |
| `item_name`               | VARCHAR(128) | NOT NULL               | → plan `points[].name`（展示）                                         |
| `category_code`           | VARCHAR(64)  | 可空                   | 存在性校验                                                             |
| `protocol_code`           | VARCHAR(64)  | 可空                   | 校验 ∈ `collector.list_protocols()`                                    |
| `data_type`               | VARCHAR(32)  | NOT NULL 默认 `float`  | → plan `points[].data_type`；合法值 `bool/int/float/double/string`     |
| `read_write`              | VARCHAR(4)   | NOT NULL 默认 `R`      | 合法值 `R` / `W` / `RW`；`W`/`RW` 才允许出现在控制接口                 |
| `unit`                    | VARCHAR(32)  | 可空                   | → plan `points[].unit`（展示，不参与换算）                             |
| `precision`               | INT          | 可空                   | 前端格式化小数位                                                       |
| `min_value` / `max_value` | DECIMAL      | 可空                   | 录入校验区间（非报警阈值）                                             |
| `address_rule`            | VARCHAR(128) | **可空但强烈建议填写** | → plan `points[].address`（`address_override` 为空时的**唯一回退源**） |
| `collection_cycle`        | INT          | 默认 1                 | 秒；→ plan `points[].cycle_ms = collection_cycle*1000`                 |
| `description`             | TEXT         | 可空                   | —                                                                      |

**必须补齐的 4 个字段（旧版缺失，是 S5/S7 的根因）**

| 新增字段        | 类型             | 默认    | 说明                                           |
|-----------------|------------------|---------|------------------------------------------------|
| `scale`         | DOUBLE NOT NULL  | `1.0`   | 工程值 = 原始值 × scale + offset，随 plan 下发 |
| `offset`        | DOUBLE NOT NULL  | `0.0`   | SQL 保留字，建表与 SQL 中统一写 `"offset"`     |
| `alarm_high`    | DOUBLE NOT NULL  | `0.0`   | 高限（工程值）                                 |
| `alarm_low`     | DOUBLE NOT NULL  | `0.0`   | 低限（工程值）                                 |
| `alarm_enabled` | BOOLEAN NOT NULL | `false` | 是否参与报警判定                               |

> **为什么必须挪到字典表**：旧版这 5 个字段只在 `using_points` 上有，而 `using_points`
> 在生产中断链下由触发器写入、且不写这几列 → 永远是默认值 → 报警判定实际"按未换算的原始值比较且阈值恒为 0"。把它们上移到
> **数据项字典**（静态、复用），再由 `running_device_collection` 可按需覆盖，plan 一次下发到位，才是正解。
>
> **`scale=0` 是已知陷阱**：`toEngineValue` 会恒等于 `offset`，表现为"该点位所有读数完全相同"。FastAPI 校验必须
> `scale != 0`（`Field(gt=0)` 或 `ne=0`），Pydantic 层直接拒绝。

### 3.7 `base_process_route_resource`（工艺路线资源）

| 字段            | 类型         | 约束                | 说明                                                         |
|-----------------|--------------|---------------------|--------------------------------------------------------------|
| `route_code`    | VARCHAR(64)  | **NOT NULL UNIQUE** | `running_equipment_route` / `base_process_route_step` 的锚点 |
| `route_name`    | VARCHAR(128) | NOT NULL            | —                                                            |
| `resource_type` | VARCHAR(32)  | 可空                | `SVG` / `PNG` / `PDF` / `JSON`                               |
| `resource_url`  | VARCHAR(512) | 可空                | 图纸/文件路径（**FastAPI 侧走静态文件服务，不入库二进制**）  |
| `thumbnail_url` | VARCHAR(512) | 可空                | —                                                            |
| `version`       | VARCHAR(32)  | 可空                | `running_equipment_route.route_version` 锁定使用版本         |
| `status`        | SMALLINT     | 默认 1              | —                                                            |
| `description`   | TEXT         | 可空                | —                                                            |

### 3.8 `base_process_route_step`（工艺路线工步）★关键

| 字段             | 类型         | 约束                                                   | 说明                                                                         |
|------------------|--------------|--------------------------------------------------------|------------------------------------------------------------------------------|
| `route_code`     | VARCHAR(64)  | NOT NULL FK → `base_process_route_resource.route_code` | `ON UPDATE CASCADE`                                                          |
| `step_code`      | VARCHAR(64)  | NOT NULL                                               | **仅在 `(route_code, step_code)` 联合唯一内唯一**，禁止单独建 UNIQUE         |
| `step_seq`       | INT          | NOT NULL                                               | 10/20/30 递增，越小越靠前                                                    |
| `step_name`      | VARCHAR(128) | NOT NULL                                               | —                                                                            |
| `device_code`    | VARCHAR(64)  | 可空 FK → `base_device_base_info.device_code`          | `ON UPDATE CASCADE ON DELETE SET NULL`：设备改名跟随，设备删除置空但工步保留 |
| `duration_time`  | INT          | 可空                                                   | 标准作业时长（秒）                                                           |
| `work_center`    | VARCHAR(128) | 可空                                                   | —                                                                            |
| `step_params`    | **JSONB**    | 默认 `{}`                                              | 关键参数（温度/扭矩/速度）；Schema 为 `dict`                                 |
| `prev_step_code` | VARCHAR(64)  | 可空                                                   | 前置工序（流程校验/并行分支）                                                |
| `is_critical`    | SMALLINT     | 默认 0                                                 | 0=否，1=关键工序                                                             |
| `status`         | SMALLINT     | 默认 1                                                 | —                                                                            |

**唯一约束**：`UNIQUE (route_code, step_code)`。

> **外键设计红线**（沿用开发指南 2.5 节结论）：`step_code` 因**仅存在于联合唯一**中，**禁止**作为其它表的直接外键目标。
> `running_route_device` 绑定工步时，必须存 `(route_code, step_code)` 组合并做联合校验，**不能**只存 `step_code` 去 FK。

### 3.9 `base_equipment_base_info`（装备主数据）

| 字段                              | 类型            | 约束                | 说明                                           |
|-----------------------------------|-----------------|---------------------|------------------------------------------------|
| `equipment_code`                  | VARCHAR(64)     | **NOT NULL UNIQUE** | `running_equipment_config.equipment_code` 锚点 |
| `equipment_name`                  | VARCHAR(128)    | NOT NULL            | —                                              |
| `customer_code` / `customer_name` | VARCHAR(64/128) | 可空                | 冗余展示                                       |
| `category_code`                   | VARCHAR(64)     | 可空                | 存在性校验                                     |
| `brand_code` / `model_code`       | VARCHAR(64)     | 可空                | —                                              |
| `location`                        | VARCHAR(256)    | 可空                | 安装位置                                       |
| `description`                     | TEXT            | 可空                | —                                              |

**语义**：装备是**比设备大的实体**（一条产线/一台加工中心），一台装备可含多台设备；装备 → 工艺路线 → 工步 → 设备 是配置链主干。

### 3.10 `base_equipment_function_dict`（装备功能字典）

| 字段             | 类型         | 约束                | 说明                                                                        |
|------------------|--------------|---------------------|-----------------------------------------------------------------------------|
| `function_code`  | VARCHAR(64)  | **NOT NULL UNIQUE** | `running_equipment_function.function_code` 锚点                             |
| `function_name`  | VARCHAR(128) | NOT NULL            | —                                                                           |
| `function_type`  | VARCHAR(32)  | NOT NULL            | `采集` / `控制` / `报警` / `追溯`                                           |
| `category_code`  | VARCHAR(64)  | 可空                | 适用装备类别                                                                |
| `equipment_code` | VARCHAR(64)  | 可空                | 适用装备（01 号文档定义，01-init.sql 实际未建此列，FastAPI 版**建议补齐**） |
| `status`         | SMALLINT     | 默认 1              | —                                                                           |
| `description`    | TEXT         | 可空                | —                                                                           |

### 3.11 跨表一致性校验清单（必须在 Service 层实现）

| 校验                                                         | 触发场景                                           | 失败响应                                             |
|--------------------------------------------------------------|----------------------------------------------------|------------------------------------------------------|
| `device_category_code` ∈ category_dict                       | 设备/装备/功能/数据项新增或更新                    | `code=400`，`message="category_code not found: XXX"` |
| `(brand_code, model_code)` ∈ brand_model_dict                | 设备/装备                                          | `code=400`                                           |
| `protocol_code` ∈ protocol_dict **且** ∈ extension protocols | 设备 `supported_protocols`、数据项 `protocol_code` | `code=400`，附 `available_protocols` 可用列表        |
| `port_code` ∈ port_dict                                      | 设备 `supported_ports`                             | `code=400`                                           |
| `item_code` ∈ data_item_dict                                 | 设备 `data_items`、采集项配置                      | `code=400`                                           |
| `step_code` ∈ `(route_code, step_code)`                      | 工步、设备绑定                                     | `code=400`                                           |
| `route_code` ∈ route_resource                                | 路线绑定                                           | `code=400`                                           |
| `function_code` ∈ function_dict                              | 功能绑定                                           | `code=400`                                           |
| `scale != 0`                                                 | 数据项                                             | Pydantic `422`（`gt=0`）                             |
| `read_write ∈ {R,W,RW}`                                      | 数据项                                             | Pydantic `422`                                       |

> **实现建议**：做一个 `services/static/ref_checker.py`，把"字典 ref 校验"收敛成一个可复用函数
> `assert_refs(session, refs: list[RefSpec])`，避免每个 Service 各写一遍。

---

## 四、B 类：运行动态配置表（5 张，FastAPI 真相源 → 编译成 plan）

### 4.0 五级配置链与编译关系

```
running_equipment_config  (L1 装备运行实例, config_status=1 才参与编译)
 ├─ running_equipment_route      (L2a 装备→工艺路线, enabled=1)
 │   └─ running_route_device     (L3 工序→设备绑定, enabled=1)  ──┐
 │       └─ running_device_collection (L4 设备→采集项, enabled=1) ─┤
 │                                                                │
 └─ running_equipment_function   (L2b 装备→功能, enabled=1)        │
                                                                  ▼
                                                     plan_compiler → Plan(dict)
                                                    → collector.apply_plan(plan, revision)
```

**编译过滤规则（硬性）**：任一环节 `enabled=0` 或 `config_status≠1`，其下游**一律不进 plan**，对应设备在运行期自然离线。这条必须在
`plan_compiler` 用 SQL 的 `WHERE` 一次过滤，**不允许在 Python 里循环判断**（否则大配置链编译会变成 N+1 查询）。

### 4.1 `running_equipment_config`（装备运行配置主表，L1）

| 字段                              | 类型         | 约束                                                 | 说明                                                                        |
|-----------------------------------|--------------|------------------------------------------------------|-----------------------------------------------------------------------------|
| `config_code`                     | VARCHAR(64)  | **NOT NULL UNIQUE**                                  | 运行配置编码，系统唯一标识                                                  |
| `equipment_code`                  | VARCHAR(64)  | NOT NULL → `base_equipment_base_info.equipment_code` | 装备模板                                                                    |
| `equipment_name`                  | VARCHAR(128) | 冗余                                                 | 列表展示                                                                    |
| `customer_code` / `customer_name` | VARCHAR      | 冗余                                                 | —                                                                           |
| `category_code`                   | VARCHAR      | 可空                                                 | —                                                                           |
| `location`                        | VARCHAR(256) | 可空                                                 | —                                                                           |
| `config_status`                   | SMALLINT     | NOT NULL 默认 0                                      | **0=未激活 / 1=已激活 / 2=已停用**（决定是否编译进 plan）                   |
| `run_status`                      | SMALLINT     | NOT NULL 默认 0                                      | 0=离线 / 1=运行中 / 2=故障 / 3=维护中（**由扩展事件派生写回，非人工配置**） |
| `remark`                          | TEXT         | 可空                                                 | —                                                                           |

**状态机（FastAPI 版，替换触发器）**

```
保存(0 未激活) ──► 可预览 plan（GET /configs/{id}/plan），不下发
      │
      ├─► 激活(1) ──► plan_compiler 编译 → revision+1 → collector.apply_plan(plan, revision)
      │                  ├─ rc=0 + plan_ack status=1 → 写 runtime_plan_revision(status=1)
      │                  └─ rc≠0 → 保留旧 revision，返回 code=409 + extension_error
      │
      └─► 停用(2) ──► 重编 plan（排除该 config）后 apply_plan → 扩展断开该设备 → 写 status=2；历史数据不动
```

> **`run_status` 的唯一写入方**：`using_device_runtime` 的 `device_status` 事件消费侧。禁止在配置 CRUD 接口里写它（旧版
> `PUT /api/running/configs/{id}` "仅更新 config_status" 的约定保留，但要显式拒绝 `run_status` 入参）。

### 4.2 `running_equipment_route`（装备→工艺路线，L2a）

| 字段            | 类型         | 约束                                                          | 说明                 |
|-----------------|--------------|---------------------------------------------------------------|----------------------|
| `config_id`     | BIGINT       | NOT NULL → `running_equipment_config.id`，`ON DELETE CASCADE` | 级联删除             |
| `route_code`    | VARCHAR(64)  | NOT NULL → `base_process_route_resource.route_code`           | —                    |
| `route_name`    | VARCHAR(128) | 冗余                                                          | —                    |
| `route_seq`     | INT          | NOT NULL 默认 10                                              | 执行次序             |
| `route_version` | VARCHAR(32)  | 可空                                                          | 锁定使用的路线版本   |
| `route_params`  | **JSONB**    | 默认 `{}`                                                     | 覆盖静态路线默认参数 |
| `enabled`       | SMALLINT     | NOT NULL 默认 1                                               | 0/1                  |

**唯一约束**：`UNIQUE (config_id, route_code)`（同一装备不应绑两条同名路线，否则 plan 会重复下发设备）。

### 4.3 `running_equipment_function`（装备→功能，L2b）

| 字段              | 类型         | 约束                                                    | 说明                |
|-------------------|--------------|---------------------------------------------------------|---------------------|
| `config_id`       | BIGINT       | NOT NULL → `running_equipment_config.id`，CASCADE       | —                   |
| `function_code`   | VARCHAR(64)  | NOT NULL → `base_equipment_function_dict.function_code` | —                   |
| `function_name`   | VARCHAR(128) | 冗余                                                    | —                   |
| `function_type`   | VARCHAR(32)  | 冗余                                                    | 采集/控制/报警/追溯 |
| `enabled`         | SMALLINT     | NOT NULL 默认 1                                         | —                   |
| `function_params` | **JSONB**    | 默认 `{}`                                               | 覆盖字典默认参数    |
| `remark`          | TEXT         | 可空                                                    | —                   |

**唯一约束**：`UNIQUE (config_id, function_code)`。**注意**：此分支**不进采集 plan**（plan 只描述设备与点位），但
`function_type='控制'` 时应影响控制接口的可下发点位白名单校验。

### 4.4 `running_route_device`（工序→设备绑定，L3）★关键

| 字段                | 类型         | 约束                                             | 说明                                                                                     |
|---------------------|--------------|--------------------------------------------------|------------------------------------------------------------------------------------------|
| `route_config_id`   | BIGINT       | NOT NULL → `running_equipment_route.id`，CASCADE | —                                                                                        |
| `route_code`        | VARCHAR(64)  | 冗余 NOT NULL                                    | 便于查询 + 联合校验用                                                                    |
| `step_code`         | VARCHAR(64)  | NOT NULL                                         | **必须与 `route_code` 联合存在于 `base_process_route_step`**（不可单独 FK，见 3.8 红线） |
| `step_name`         | VARCHAR(128) | 冗余                                             | —                                                                                        |
| `step_seq`          | INT          | 可空                                             | 可覆盖静态工步次序                                                                       |
| `device_code`       | VARCHAR(64)  | NOT NULL → `base_device_base_info.device_code`   | `ON UPDATE CASCADE ON DELETE CASCADE`                                                    |
| `device_name`       | VARCHAR(128) | 冗余                                             | —                                                                                        |
| `device_connection` | **JSONB**    | NOT NULL 默认 `{}`                               | **连接参数的运行期覆盖**，协议相关结构见第六章                                           |
| `port_code`         | VARCHAR(64)  | 可空 → `base_device_port_dict.port_code`         | —                                                                                        |
| `protocol_code`     | VARCHAR(64)  | NOT NULL 默认 `MODBUS_TCP`                       | 优先于 `base_device_base_info.supported_protocols`，**决定 plan 的 `protocol`**          |
| `enabled`           | SMALLINT     | NOT NULL 默认 1                                  | —                                                                                        |

**关键约束**：

- `UNIQUE (route_config_id, step_code, device_code)` —— 同一工序可绑多台设备，但同一台不可在一工序内重复；
- `protocol_code` 必须 ∈ `collector.list_protocols()`（归一化比对），这是**编译期唯一硬门槛**，不满足则 `apply_plan` 必然被拒；
- `device_connection` 不做 JSON Schema 强校验（因协议而异），但必须在 `plan_compiler` 阶段按协议调用
  `validate_connection(protocol, conn)`（见第六章表 6.2）。

### 4.5 `running_device_collection`（设备→采集项，L4）★关键

| 字段                                         | 类型         | 约束                                              | plan 映射                                                            |
|----------------------------------------------|--------------|---------------------------------------------------|----------------------------------------------------------------------|
| `device_binding_id`                          | BIGINT       | NOT NULL → `running_route_device.id`，CASCADE     | 分组依据                                                             |
| `device_code`                                | VARCHAR(64)  | 冗余 NOT NULL                                     | plan `device_key`                                                    |
| `item_code`                                  | VARCHAR(64)  | NOT NULL → `base_device_data_item_dict.item_code` | 字典锚点                                                             |
| `item_name`                                  | VARCHAR(128) | 冗余                                              | plan `points[].name`                                                 |
| `collection_cycle`                           | INT          | 可空                                              | 秒；空则取字典 `collection_cycle` → `cycle_ms`                       |
| `address_override`                           | VARCHAR(128) | 可空                                              | 空则取字典 `address_rule` → `address`（**不回退 item_code**，见 S6） |
| `data_type` / `read_write`                   | VARCHAR      | 冗余，可空                                        | 空则取字典                                                           |
| `scale` / `offset`                           | DOUBLE       | **新增，可空**                                    | 空则取字典（否则无法覆盖换算）                                       |
| `alarm_high` / `alarm_low` / `alarm_enabled` | DOUBLE/BOOL  | **新增，可空**                                    | 空则取字典（否则报警阈值无法配置）                                   |
| `enabled`                                    | SMALLINT     | NOT NULL 默认 1                                   | —                                                                    |

**关键约束**：

- `UNIQUE (device_binding_id, item_code)`；
- **编译必须校验 `address` 最终值非空**：`COALESCE(NULLIF(address_override,''), data_item.address_rule)` 若为空 → 编译失败并
  **指明是哪条采集项**（旧版靠 item_code 兜底制造了大量"地址是业务编码"的脏数据，新规则必须显式报错）；
- `read_write='W'` 的点位可不出现在 plan 的采集列表（只用于控制），但必须保留在配置中以支持控制接口白名单。

### 4.6 plan 编译 SQL 骨架（`services/plan_compiler.py` 对应查询）

```sql
-- 单 config 全链展开（一次查询，禁止 N+1）
SELECT c.id AS config_id, c.config_code, c.equipment_code, c.equipment_name,
       r.id AS route_config_id,
       d.id AS binding_id, d.device_code, d.device_name, d.protocol_code,
       d.device_connection, d.port_code,
       col.id AS collection_id, col.item_code, col.item_name,
       COALESCE(NULLIF(col.address_override,''), it.address_rule)      AS address,
       COALESCE(col.collection_cycle, it.collection_cycle, 1) * 1000    AS cycle_ms,
       COALESCE(col.data_type,  it.data_type,  'float')                 AS data_type,
       COALESCE(col.scale,      it.scale,      1.0)                     AS scale,
       COALESCE(col."offset",   it."offset",   0.0)                     AS offset,
       COALESCE(col.alarm_high, it.alarm_high, 0.0)                     AS alarm_high,
       COALESCE(col.alarm_low,  it.alarm_low,  0.0)                     AS alarm_low,
       COALESCE(col.alarm_enabled, it.alarm_enabled, false)             AS alarm_enabled,
       col.read_write
FROM running_equipment_config c
JOIN running_equipment_route r        ON r.config_id = c.id AND r.enabled = 1
JOIN running_route_device d           ON d.route_config_id = r.id AND d.enabled = 1
JOIN running_device_collection col    ON col.device_binding_id = d.id AND col.enabled = 1
JOIN base_device_data_item_dict it    ON it.item_code = col.item_code
WHERE c.id = $1 AND c.config_status = 1;
```

> **注意 `offset` 双引号**：它是 SQL 保留字，建表、查询、结果集取值三处都必须写 `"offset"`，否则报语法错误或取不到值。这是旧
> C++ 代码里明确记录过的坑，Python 侧 `asyncpg` 的 Record 字段名同样需要 `row['offset']`（asyncpg 已去掉引号，但 SQL 文本必须带）。

**编译产物结构**（与主文档 7.2 节 `plan` 一致）：`device_code` 相同的行合并为一个 `devices[]` 元素，其下聚合成 `points[]`。同一
`device_code` 在多个工序/多条路线出现时，**按 `device_code` 去重**（沿用旧版 `DISTINCT ON` 语义），并保留**最短周期**的采集项。

---

## 五、C 类：运行期结果与快照表（FastAPI 业务侧写入）

### 5.1 `using_devices`（设备运行态快照）—— 语义已变更

**重构后定位**：不再是"配置同步产物"，而是**扩展运行态快照**。写入时机：`plan_ack` 成功后由 `collector_orchestrator`
写一次；后续只更新 `status` 与 `updated_at`。

| 字段                                | 类型         | 约束                       | 写入方                                                  |
|-------------------------------------|--------------|----------------------------|---------------------------------------------------------|
| `id`                                | BIGSERIAL PK | —                          | FastAPI                                                 |
| `device_code`                       | VARCHAR(64)  | **NOT NULL UNIQUE**        | 来自 plan `device_key`                                  |
| `device_name`                       | VARCHAR(128) | NOT NULL                   | 来自 plan                                               |
| `equipment_code` / `equipment_name` | VARCHAR      | 冗余，`DEFAULT ''`         | 来自 `running_equipment_config`                         |
| `remark`                            | TEXT         | 可空                       | —                                                       |
| `protocol_code`                     | VARCHAR(64)  | NOT NULL 默认 `MODBUS_TCP` | 来自 plan                                               |
| `device_connection`                 | **JSONB**    | NOT NULL 默认 `{}`         | 来自 plan                                               |
| `status`                            | SMALLINT     | NOT NULL 默认 0            | **仅由 `device_status` 事件写**（0=离线 1=在线 2=异常） |
| `created_at` / `updated_at`         | BIGINT       | Unix 秒                    | FastAPI                                                 |

**索引**：`status` / `protocol_code` / `equipment_code`（保留旧版三个索引）。

> **禁止**在此表写"用户手工设备" —— 旧版允许 `DeviceService` 手工 CRUD，导致触发器清理时可能误删。新规则：**`using_devices`
> 每行都必须有对应的 plan 来源**，无来源行视为脏数据，由巡检任务清理并告警。

### 5.2 `using_points`（采集点快照）

| 字段                       | 类型         | 约束                                                                             | 说明                                       |
|----------------------------|--------------|----------------------------------------------------------------------------------|--------------------------------------------|
| `device_code`              | VARCHAR(64)  | NOT NULL FK → `using_devices.device_code`，`ON UPDATE CASCADE ON DELETE CASCADE` | 业务键外键                                 |
| `item_name`                | VARCHAR(128) | NOT NULL                                                                         | 展示名                                     |
| `address`                  | VARCHAR(128) | NOT NULL                                                                         | Modbus 寄存器 / OPC UA NodeId / MQTT topic |
| `data_type`                | VARCHAR(32)  | NOT NULL 默认 `float`                                                            | `bool/int/float/double/string`             |
| `unit`                     | VARCHAR(32)  | 默认 `''`                                                                        | 仅展示                                     |
| `scale`                    | DOUBLE       | NOT NULL 默认 1.0                                                                | 工程值 = 原始值 × scale + offset           |
| `"offset"`                 | DOUBLE       | NOT NULL 默认 0.0                                                                | **保留字，必须双引号**                     |
| `collection_cycle`         | INT          | NOT NULL 默认 1                                                                  | 秒                                         |
| `alarm_high` / `alarm_low` | DOUBLE       | NOT NULL 默认 0.0                                                                | 工程值阈值                                 |
| `alarm_enabled`            | BOOLEAN      | NOT NULL 默认 FALSE                                                              | —                                          |
| `enabled`                  | BOOLEAN      | NOT NULL 默认 TRUE                                                               | —                                          |

**唯一约束**：`UNIQUE (device_code, item_name)`、`UNIQUE (device_code, address)`。

> 这两个唯一约束是**旧版遗留的强限制**：同一设备下不能有两个同名点位，也不能有两个同地址点位。Modbus
> 场景下"同一地址不同类型两套语义"（如 40001 同时当 int16 与 bit）**无法表达**。FastAPI 版建议把唯一键改为
> `UNIQUE (device_code, address, data_type)`，并在迁移脚本中同步调整（属 P2 阶段待确认项）。

### 5.3 `time_series_data`（PostgreSQL 普通表）★数据量最大

| 字段        | 类型                                            | 说明                                        |
|-------------|-------------------------------------------------|---------------------------------------------|
| `ts`        | TIMESTAMPTZ NOT NULL                            | **分区键**，毫秒精度                        |
| `point_id`  | BIGINT NOT NULL FK → `using_points.id`，CASCADE | —                                           |
| `value`     | DOUBLE NOT NULL                                 | 工程值（已换算）                            |
| `quality`   | VARCHAR(16) NOT NULL 默认 `Good`                | `Good` / `Bad` / `Uncertain`（OPC UA 标准） |
| `raw_value` | TEXT                                            | 原始值，排查 scale/offset 用                |

**普通表配置（保持旧版策略）**

| 项                    | 值                                          |
|-----------------------|---------------------------------------------|
| `chunk_time_interval` | `1 day`（点位 > 5000 时改 12 小时）         |
| 压缩                  | `segmentby = point_id`，`orderby = ts DESC` |
| `compression_policy`  | 7 天                                        |
| `retention_policy`    | 90 天                                       |

**连续聚合视图**

| 视图                | 粒度           | 字段                                                                      |
|---------------------|----------------|---------------------------------------------------------------------------|
| `cagg_ts_data_1min` | 1 分钟 × point | `value_avg` / `value_max` / `value_min` / `sample_count` / `last_quality` |
| `cagg_ts_data_1h`   | 1 小时 × point | 同上                                                                      |

**写入方式变更（S4）**：从"逐点 INSERT + `ON CONFLICT (ts, point_id) DO NOTHING`"改为 **`COPY` 攒批**：

```python
await conn.copy_records_to_table(
    "time_series_data",
    records=[(ts, point_id, value, quality, raw_value), ...],
    columns=["ts", "point_id", "value", "quality", "raw_value"],
)
```

> **注意**：`COPY` 不支持 `ON CONFLICT`。去重必须在攒批阶段按 `(ts, point_id)`
> 在内存去重（同批内同点位同毫秒只留最后一条），跨批重复靠"扩展每个采样周期只进快照一次"保证。若接受极小概率重复，可保留
> CLUSTER 侧 `INSERT ... ON CONFLICT DO NOTHING` 的降级路径给低吞吐场景。

### 5.4 `using_alarms`（报警记录）

| 字段                               | 类型         | 约束                 | 说明                                                           |
|------------------------------------|--------------|----------------------|----------------------------------------------------------------|
| `device_code`                      | VARCHAR(64)  | NOT NULL FK，CASCADE | —                                                              |
| `point_address`                    | VARCHAR(128) | 可空                 | **业务键定位采集点**（与 `device_code` 组合），不用 `point_id` |
| `point_name`                       | VARCHAR(128) | 可空                 | 冗余，点位删除后仍可展示                                       |
| `level`                            | SMALLINT     | NOT NULL 默认 1      | 1=提示 2=一般 3=严重 4=紧急                                    |
| `state`                            | SMALLINT     | NOT NULL 默认 1      | 1=活动 2=已确认 3=已恢复                                       |
| `message`                          | TEXT         | 可空                 | —                                                              |
| `value`                            | DOUBLE       | NOT NULL 默认 0.0    | 触发时的工程值                                                 |
| `triggered_at`                     | BIGINT       | NOT NULL             | Unix 秒                                                        |
| `acknowledged_at` / `recovered_at` | BIGINT       | 可空                 | —                                                              |

**为什么不用 `point_id`**（沿用 02.03 号文档结论）：`using_points` 在重新下发时 id 可能变化（快照重建），用自增 id
关联会让历史报警指向错误点位；用 `(device_code, point_address)` 业务键 + `point_name` 冗余才能保证历史可追溯。

**判定规则（迁到内存态，S5）**

```
输入：read_batch.sample = {device_key, point_id, value, quality, alarm_high, alarm_low, alarm_enabled}
① alarm_enabled = False           → 跳过
② quality != Good                 → 跳过（坏值不报警，避免断链刷屏）
③ value > alarm_high 且无活动报警  → trigger(level=2)
④ value < alarm_low  且无活动报警  → trigger(level=2)
⑤ 超限幅度 > 阈值区间的 50%        → 升为 level=3
⑥ 回落到正常区间 且存在 state=1   → recover()
```

内存态结构（每 worker 一份，启动时从 DB 预热 `state=1` 的报警）：

```python
active: dict[tuple[str, str], ActiveAlarm]   # key = (device_code, point_address)
```

> **跨 worker 一致性**：多 worker 下同一报警可能被两个 worker 同时判定触发 → 重复落库。解决：报警 upsert 用
> `INSERT ... ON CONFLICT (device_code, point_address) WHERE state = 1 DO NOTHING` 的**部分唯一索引**，或把报警判定收敛到
> **单一 `ibmcp-ingest` worker**（推荐，见主文档 9.6 WS 拆分同理）。

### 5.5 `control_commands`（控制命令台账）

| 字段          | 类型         | 约束                                      | 说明                                                   |
|---------------|--------------|-------------------------------------------|--------------------------------------------------------|
| `device_id`   | BIGINT       | NOT NULL FK → `using_devices.id`，CASCADE | 注意：**用自增 id 而非 device_code**（旧版如此，保留） |
| `point_id`    | BIGINT       | NOT NULL FK → `using_points.id`，CASCADE  | —                                                      |
| `point_name`  | VARCHAR(128) | 可空                                      | 冗余                                                   |
| `value`       | TEXT         | NOT NULL                                  | 写入值字符串化，业务层解析                             |
| `status`      | SMALLINT     | NOT NULL 默认 0                           | **0=待下发 1=下发中 2=成功 3=失败 4=超时**             |
| `message`     | TEXT         | 可空                                      | Modbus 异常码 / 错误信息                               |
| `issued_at`   | BIGINT       | NOT NULL                                  | Unix 秒                                                |
| `finished_at` | BIGINT       | 可空                                      | —                                                      |

**状态机（FastAPI 版，异步回执）**

```
issue() → status=0（待下发）
   │ collector.write_point(device_key, point_id, value) 立即返回 command_id（已入队）
   ▼
status=1（下发中，扩展事件前由 FastAPI 置位）
   │ write_result 事件
   ├─ success=true  → status=2，finished_at
   └─ success=false → status=3；超时（>5s 无事件）→ status=4
```

> **旧版 vs 新版**：旧版是同步 `connect → writePoint → disconnect` 全链路阻塞在 HTTP 请求里；新版是"入队返回 +
> 事件回执"。若前端需要同步行为，调用 `/control?wait=true`（最长 5s）。

---

## 六、关键字段契约：协议与连接参数（跨表共用）

### 6.1 `device_connection` JSONB 结构（按协议）

来源：`running_route_device.device_connection`（覆盖 `base_device_base_info` 的默认值）。FastAPI 在 `plan_compiler`
中原样透传给扩展，**不做键名重写**。

| 协议（plan 中的 `protocol`） | `device_connection` 结构                                                                                                                                                 |
|------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `modbus_tcp`                 | `{"host":"192.168.1.10","port":502,"slave":1,"timeout_ms":3000,"batch_gap":8}`                                                                                           |
| `modbus_rtu`                 | `{"device":"/dev/ttyUSB0","baud":9600,"parity":"N","data_bits":8,"stop_bits":1,"slave":1}`                                                                               |
| `opc_ua`                     | `{"endpoint":"opc.tcp://192.168.1.20:4840","security_policy":"None","username":"","password":""}`                                                                        |
| `mqtt`                       | `{"host":"127.0.0.1","port":1883,"client_id":"ibmcp-collector","username":"","password":"","qos":1,"keep_alive":60,"subscribe_wait_ms":1500,"max_message_age_ms":30000}` |
| `web_socket`                 | `{"url":"ws://192.168.1.30:9000/ws","subscribe_msg":"{...}"}`                                                                                                            |
| `socket_tcp` / `socket_udp`  | `{"host":"192.168.1.40","port":5000,"proto":"tcp","send_hex":"...","parse_rule":"..."}`                                                                                  |

> **键名必须与 04 号 4.3 逐字一致（全蛇形）**：扩展的 `connection_json` 解析不做任何驼峰↔蛇形映射，`d["timeout_ms"]` 找不到就报
> 4002。任何一处写成 `timeoutMs` 都会在联调时暴露为"配置保存成功、下发报连接参数非法"。

### 6.2 连接参数校验（`validate_connection`）

旧版 `IDeviceDriver::validateParams()` 的行为必须在 FastAPI 侧提前复刻一份（`plan_compiler`
内），避免"配置保存成功、下发才失败"的体验问题。

| 协议         | 必填键         | 类型/范围            | 常见失败                          |
|--------------|----------------|----------------------|-----------------------------------|
| `modbus_tcp` | `host`, `port` | `1..65535`           | 端口填 0、host 为空               |
| `modbus_rtu` | `device`       | 以 `/dev/` 开头      | 未透传串口、用户不在 `dialout` 组 |
| `opc_ua`     | `endpoint`     | 以 `opc.tcp://` 开头 | 协议头写错                        |
| `mqtt`       | `host`, `port` | —                    | `client_id` 重复（多实例冲突）    |
| `web_socket` | `url`          | `ws://` / `wss://`   | —                                 |
| `socket_*`   | `host`, `port` | —                    | `parse_rule` 缺失导致解析不出值   |

> 校验失败时接口返回 `code=400`，`message` 明确指出"哪个协议缺哪个键"，而不是等 `apply_plan` 返回
> `4001 protocol not supported`。

### 6.3 MQTT 的两个时间参数（易错，必须写进文档与配置模板）

| 参数                 | 默认  | 作用                                                     |
|----------------------|-------|----------------------------------------------------------|
| `subscribe_wait_ms`  | 1500  | `connect()` 内订阅后等待首条消息的最长时间               |
| `max_message_age_ms` | 30000 | **消息新鲜度阈值**；缓存消息超过此值即判定陈旧并返回失败 |

**必须提醒现场**：MQTT 是事件驱动，设备不推送就没有新值。若设备上报周期 > 30s，**必须把 `max_message_age_ms`
调到大于上报周期**，否则会周期性误判失败。反之若设得太大，会把 1 小时前的旧值当当前值持续入库，表现为"曲线卡住但无报错"。

### 6.4 采集周期与地址语义（跨表统一口径）

| 概念     | 唯一口径                                                                                     | 说明                                                   |
|----------|----------------------------------------------------------------------------------------------|--------------------------------------------------------|
| 采集周期 | `running_device_collection.collection_cycle`（秒，可空）→ 字典 `collection_cycle` → 默认 `1` | 下发时统一转 `cycle_ms`（= `collection_cycle * 1000`） |
| 通信地址 | `address_override` → 字典 `address_rule`                                                     | **绝不用 `item_code` 兜底**                            |
| 换算     | `scale` / `offset`（`running_device_collection` 覆盖 → 字典）                                | 只在驱动层做一次                                       |
| 报警阈值 | `alarm_high/low/enabled`（覆盖 → 字典）                                                      | 在**工程值**上判定                                     |
| 数据类型 | `data_type`（覆盖 → 字典），默认 `float`                                                     | 决定解码宽度                                           |

### 6.5 Modbus 批量读常量（扩展侧，配置不可覆盖）

| 常量                 | 值   | 说明                                                                                                   |
|----------------------|------|--------------------------------------------------------------------------------------------------------|
| `batch_gap`          | 8    | 两个点位地址间允许的空洞，≤8 才合并成一帧。**已做成连接参数**（`device_connection.batch_gap`），默认 8 |
| `kMaxBlockRegisters` | 125  | 单帧最多寄存器（Modbus 协议上限，**不可调大**）                                                        |
| `kMaxBlockBits`      | 2000 | 单帧最多位（线圈/离散输入上限，**不可调大**）                                                          |

---

## 七、D 类：系统管理表（RBAC）

### 7.1 `roles`

| 字段          | 类型        | 约束                                                              |
|---------------|-------------|-------------------------------------------------------------------|
| `name`        | VARCHAR(64) | **NOT NULL UNIQUE**：`admin` / `engineer` / `operator` / `viewer` |
| `description` | TEXT        | 可空                                                              |

**预设角色与权限范围（前端路由守卫 + 后端接口鉴权共用）**

| name       | 权限范围                     | 可访问模块                                     |
|------------|------------------------------|------------------------------------------------|
| `admin`    | 全部                         | 系统管理 + 基础数据 + 运行配置 + 控制监控      |
| `engineer` | 设备与点位配置、下发控制     | 基础数据 + 运行配置 + 控制监控（系统管理只读） |
| `operator` | 查看数据、确认报警、有限控制 | 控制监控（只读 + ack）                         |
| `viewer`   | 纯只读                       | 控制监控（只读）                               |

### 7.2 `users`

| 字段            | 类型         | 约束                | FastAPI 变更                                                                        |
|-----------------|--------------|---------------------|-------------------------------------------------------------------------------------|
| `username`      | VARCHAR(64)  | **NOT NULL UNIQUE** | —                                                                                   |
| `password_hash` | VARCHAR(255) | NOT NULL            | **改 argon2id**；`$pbkdf2$` 前缀的旧哈希走兼容校验分支，校验成功后自动重写为 argon2 |
| `display_name`  | VARCHAR(128) | 可空                | —                                                                                   |
| `enabled`       | BOOLEAN      | NOT NULL 默认 TRUE  | 禁用后无法登录                                                                      |
| `created_at`    | BIGINT       | NOT NULL            | Unix 秒                                                                             |

**必改项**：预置管理员 `admin / admin123` 属**已知弱口令**，生产部署脚本必须首次启动时替换（或强制首次登录改密）。JWT
密钥同样不得沿用 `ibmcp-change-me-in-production`。

### 7.3 `user_roles`

| 字段      | 类型   | 约束                              |
|-----------|--------|-----------------------------------|
| `user_id` | BIGINT | NOT NULL FK → `users.id`，CASCADE |
| `role_id` | BIGINT | NOT NULL FK → `roles.id`，CASCADE |

**联合主键** `(user_id, role_id)`。

**登录响应变更（S8 连带修复）**：旧版 `POST /api/auth/login` 只返回 `{token, username, displayName, roles[]}`，**缺 `id`**
，导致前端要额外查一次用户列表。FastAPI 版必须在 login 响应补 `id`（前端 `authStore` 已为此留了兼容注释）。

---

## 八、E 类：重构新增表（3 张）

### 8.1 `runtime_plan_revision`（计划版本记录）

| 字段                        | 类型         | 约束                                 | 说明                                           |
|-----------------------------|--------------|--------------------------------------|------------------------------------------------|
| `revision`                  | BIGSERIAL PK | —                                    | 计划版本号，即为 `apply_plan` 的 `revision`    |
| `config_id`                 | BIGINT       | 可空 → `running_equipment_config.id` | 单 config 下发时填写；批量下发为 NULL          |
| `plan_hash`                 | VARCHAR(64)  | NOT NULL                             | plan JSON 的 SHA-256，用于"内容未变不重复下发" |
| `status`                    | SMALLINT     | NOT NULL 默认 0                      | **0=已下发 1=生效 2=失败**                     |
| `extension_error`           | TEXT         | 可空                                 | `apply_plan` 失败时的 `err_json`               |
| `created_at` / `applied_at` | BIGINT       | —                                    | Unix 秒                                        |

**用途**：

1. 幂等：`plan_hash` 相同则跳过下发；
2. 崩溃恢复：进程重启后取 `status=1` 的最新 revision，重放 `plan_json`（主文档 7.7）；
3. 回滚：`status=2` 时按 `revision-1` 重新下发上一版。

### 8.2 `audit_config_change`（配置变更审计）

| 字段                                           | 类型        | 说明                                                                                                 |
|------------------------------------------------|-------------|------------------------------------------------------------------------------------------------------|
| `target_level`                                 | VARCHAR(16) | `equipment_config` / `equipment_route` / `equipment_function` / `route_device` / `device_collection` |
| `target_id`                                    | BIGINT      | 对应运行表 id                                                                                        |
| `action`                                       | VARCHAR(16) | `create` / `update` / `delete` / `activate` / `deactivate`                                           |
| `plan_revision_before` / `plan_revision_after` | BIGINT      | 变更前后版本（未触发下发为 NULL）                                                                    |
| `operator`                                     | VARCHAR(64) | 操作人（取 JWT 主体）                                                                                |
| `detail`                                       | **JSONB**   | 字段级 diff                                                                                          |
| `created_at`                                   | BIGINT      | Unix 秒                                                                                              |

> 这是**旧版完全缺失的能力**。旧版配置变更无版本、无回执、无审计，出问题只能翻旧版日志。FastAPI 版把"谁在什么时候把哪一级配置改成什么、触发了哪一版
> plan、成功没有"全部留痕。

### 8.3 `using_device_runtime`（设备运行态快照）

| 字段                        | 类型           | 说明                                                |
|-----------------------------|----------------|-----------------------------------------------------|
| `device_key`                | VARCHAR(96) PK | 同 plan `device_key`                                |
| `protocol_code`             | VARCHAR(64)    | —                                                   |
| `status`                    | SMALLINT       | 0=离线 1=在线 2=异常，来自扩展 `device_status` 事件 |
| `last_error`                | TEXT           | 最近一次失败原因                                    |
| `points_ok` / `points_fail` | INT            | 最近一个采样窗口内成功/失败点位数                   |
| `snapshot_at`               | BIGINT         | Unix 秒                                             |

**用途**：`GET /api/v1/runtime/devices/{device_key}` 的数据源；同时是 `using_devices.status` 的上游（事件 → 运行态快照 → 同步
status）。

> **表名说明**：旧稿写作 `kernel_runtime_snapshot`。本次统一为 `using_device_runtime` —— 与 `using_devices` /
> `using_points` 同族，一眼能看出它是运行态快照而非配置源头；同时 "kernel" 一词已废弃，统一称"扩展"，不再出现在任何表名、字段名与接口名里。

---

## 九、字段级迁移清单（旧 → 新，落地时逐条核对）

### 9.1 必须新增的字段

| 表                                                                       | 新增字段                                                      | 原因                                                    |
|--------------------------------------------------------------------------|---------------------------------------------------------------|---------------------------------------------------------|
| `base_device_data_item_dict`                                             | `scale`、`offset`、`alarm_high`、`alarm_low`、`alarm_enabled` | S5/S7：报警与换算阈值必须在字典层表达，才能随 plan 下发 |
| `running_device_collection`                                              | `scale`、`offset`、`alarm_high`、`alarm_low`、`alarm_enabled` | 同上，且支持运行期覆盖                                  |
| `base_device_protocol_dict`                                              | `has_driver BOOLEAN`                                          | 标记字典项是否有扩展驱动（4 个预留协议置 false）        |
| `base_equipment_function_dict`                                           | `equipment_code`                                              | 01 号文档已定义，01-init.sql 未建，补齐                 |
| `runtime_plan_revision` / `audit_config_change` / `using_device_runtime` | 整表                                                          | 见第八章                                                |

### 9.2 必须修改的约束

| 表                             | 旧约束                                                              | 新约束                                              | 原因                             |
|--------------------------------|---------------------------------------------------------------------|-----------------------------------------------------|----------------------------------|
| `base_device_brand_model_dict` | `brand_code` 单列唯一                                               | `UNIQUE (brand_code, model_code)`                   | 一个品牌应有多个型号             |
| `running_equipment_route`      | 无                                                                  | `UNIQUE (config_id, route_code)`                    | 防重复下发                       |
| `running_equipment_function`   | 无                                                                  | `UNIQUE (config_id, function_code)`                 | 防重复                           |
| `running_route_device`         | 无                                                                  | `UNIQUE (route_config_id, step_code, device_code)`  | 防重复绑定                       |
| `running_device_collection`    | 无                                                                  | `UNIQUE (device_binding_id, item_code)`             | 防重复采集                       |
| `using_points`                 | `UNIQUE (device_code, item_name)` + `UNIQUE (device_code, address)` | **建议** `UNIQUE (device_code, address, data_type)` | 允许同址多类型语义（待业务确认） |

### 9.3 必须废弃的机制

| 废弃项                                                                                                                                 | 位置                        | 替代                                                                           |
|----------------------------------------------------------------------------------------------------------------------------------------|-----------------------------|--------------------------------------------------------------------------------|
| 触发器 `trg_running_config_sync` / `trg_running_config_cleanup` / `trg_running_config_delete_cleanup`                                  | `01-init.sql` 18 章         | `plan_compiler` + `collector.apply_plan`                                       |
| 触发器 `trg_route_device_sync` / `trg_device_collection_sync` / `trg_equipment_route_delete_mark`                                      | 同上                        | `audit` 拦截 + 重新编译下发                                                    |
| 函数 `sync_config_to_using_tables` / `sync_all_configs_to_using_tables` / `cleanup_config_from_using_tables` / `cleanup_orphan_device` | 同上（17 章）               | Python `collector_orchestrator`                                                |
| 事务级变量 `ibmcp.deleting_config` / `ibmcp.deleting_route_config`                                                                     | 同上                        | 无（不再需要）                                                                 |
| 路由 `POST /api/running/configs/{id}/sync` / `/{id}/cleanup`                                                                           | `EquipmentConfigController` | 降级为**运维兜底接口**，仅用于修复"快照与 plan 不一致"的异常，正常流程不应调用 |
| `users.password_hash` 的 PBKDF2                                                                                                        | `CryptoUtil.h`              | argon2id（保留校验兼容）                                                       |

---

## 十、与 01/02/03 号数据字典的对照索引

| 原始文档                                             | 章节                                               | 本文档对应                            |
|------------------------------------------------------|----------------------------------------------------|---------------------------------------|
| `01-静态数据表字段定义.md`                           | 1~10 节（10 张 base 表）                           | 第三章 3.1~3.10                       |
| 同上                                                 | 11 节表关系说明                                    | 3.11 跨表校验清单                     |
| `02-运行动态配置表字段定义.md`                       | 1~5 节（5 张 running 表）                          | 第四章 4.1~4.5                        |
| 同上                                                 | 表关系总览 / 写入顺序 / 静态-运行映射              | 4.0 五级链 + 4.6 编译 SQL             |
| `03-运行期核心与系统管理表字段定义.md`               | 一、1~5（using_devices/points/ts/alarms/commands） | 第五章 5.1~5.5                        |
| 同上                                                 | 二、6~8（roles/users/user_roles）                  | 第七章                                |
| `开发指南 02.03`（`running_* → using_*` 触发器同步） | 全文                                               | 第二章 S1/S2 + 第九章 9.3（整体废弃） |
| `开发指南 04`（协议完善指南）                        | 一/三/四（协议清单、参数契约、依赖）               | 3.5 协议字典 + 第六章                 |
| `开发指南 04`                                        | 第 7.1~7.3（数据源/触发器/并发）                   | 第二章 + 5.1 + 6.4                    |
| `开发指南 01`                                        | 2.5 数据库设计 / 2.6 驱动工厂                      | 第三章 + 第六章                       |
| `开发指南 03-API`                                    | 0.2 统一响应 / 0.3 分页 / 4.4 报警规则             | 主文档 5.5 + 本文档 5.4               |

---

## 十一、落地检查清单（建表前逐条核对）

| #  | 检查项                                                                                    | 影响                            |
|----|-------------------------------------------------------------------------------------------|---------------------------------|
| 1  | 数据项字典已补 `scale/offset/alarm_*` 5 列，且 `scale` 有 `CHECK (scale <> 0)`            | 否则报警与换算永远失效（S5/S7） |
| 2  | 采集项配置已补 5 个覆盖列                                                                 | 否则无法运行期覆盖              |
| 3  | `address_rule` 全表非空（除 `read_write='W'` 的纯控制项）                                 | 否则 plan 编译失败              |
| 4  | `protocol_code` 全部能在 `collector.list_protocols()` 中找到对应                          | 否则 `apply_plan` 返回 4001     |
| 5  | `running_route_device` 的 `(route_code, step_code)` 全部在静态工步中存在                  | 否则绑定无效                    |
| 6  | 三个 `UNIQUE` 复合约束已建                                                                | 否则 plan 重复下发设备          |
| 7  | 触发器链已删除，且 `POST /sync`、`/cleanup` 已改为"运维兜底"语义并在文档/前端标注         | 否则双写冲突                    |
| 8  | `users.password_hash` 迁移脚本已准备好 PBKDF2 → argon2 的兼容校验                         | 否则存量用户无法登录            |
| 9  | `using_points` 唯一键变更方案已与业务确认                                                 | 影响 Modbus 同址多类型场景      |
| 10 | `runtime_plan_revision` / `audit_config_change` / `using_device_runtime` 已建并入 Alembic | 否则无版本、无审计、无运行态    |

---

## 十二、待业务确认（与主文档 Q 清单对应）

| 编号 | 问题                                                                          | 本文档关联 |
|------|-------------------------------------------------------------------------------|------------|
| D1   | `using_points` 唯一键是否改为 `(device_code, address, data_type)`？           | 9.2        |
| D2   | 报警判定是否收敛到单一 ingest worker（牺牲吞吐换一致性）？                    | 5.4        |
| D3   | `run_status` 是否允许人工覆盖（如检修期强制标"维护中"）？                     | 4.1        |
| D4   | `time_series_data` 是否仍需 90 天保留，或按点位分级？                         | 5.3        |
| D5   | 4 个无驱动的预留协议（PROFINET/PROFIBUS_DP/CCLINK/S7_COMM）是否保留在字典中？ | 3.5        |