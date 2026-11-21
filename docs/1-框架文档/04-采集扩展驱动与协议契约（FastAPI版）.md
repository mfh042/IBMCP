# IBMCP 采集扩展驱动能力与协议契约（FastAPI 版）

>
版本：v1.1　　编写日期：2026-11-18　　配套：[01-架构设计](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/01-IBMCP-FastAPI重构架构设计.md) · [02-数据模型](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/02-数据模型与字段契约（FastAPI版）.md) · [03-接口契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/03-接口契约与功能对照（FastAPI版）.md)
>
> **v1.1 变更（2026-11-20）**：① §7.4 构建安装口径由 `pip install ./backend` 改为项目根 `pip install .`（`pyproject.toml` 在项目根，`backend/` 下无 `pyproject.toml`，故不用 `pip install ./backend`）；② §8.1 构建依赖表 `python3-devel` 改为 `python3.11-devel`（仅头文件，由 `build.sh` 按需补装），明确解释器用系统自带 python3.11、不单独装。
> **本文档定位**：把 `开发指南/04-装备通讯协议完善指南` 中的
> **协议实现现状、驱动契约、地址语义、并发模型、三套工业库实测结果、欧拉侧依赖清单**，重写为 **C++ 采集扩展（Python 包
> `industrial_collector`，内部模块 `_core`）** 的设计依据。
> 与原文档的根本差异：原文档描述的是"Drogon 进程内的 CollectorService 线程模型"，本文档描述的是"**以 pybind11 扩展形式被
> FastAPI 同进程 import 的采集库**"的线程与内存模型。采集逻辑可大量复用，但**生命周期、线程所有权、错误传播方式全部改变**。
>
> 接口形态、构建方式、GIL
> 与异常边界以 [05-绑定与分层契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/05-pybind11绑定与C++Python分层契约.md)
> 为准；本文档只负责**驱动与协议侧契约**。

---

## 一、协议能力现状（作为扩展 v1 的实现基线）

### 1.1 7 项协议的真实实现状态

| 协议       | 扩展对外的 `protocol` 规范名         | 底层库                        | 状态          | 扩展 v1 关键实现点                                                  |
|------------|--------------------------------------|-------------------------------|---------------|---------------------------------------------------------------------|
| Modbus TCP | `modbus_tcp`                         | libmodbus 3.1.11              | ✅ 已实现     | 批量读合并，125 寄存器 / 2000 位单帧上限                            |
| Modbus RTU | `modbus_rtu`                         | libmodbus（共用寄存器逻辑）   | ✅ 已实现     | 仅 ctx 不同；需串口透传 + `dialout` 组                              |
| OPC UA     | `opc_ua`                             | open62541 1.4.11              | ✅ 已实现     | NodeId 解析、读写；注意 1.4 与 1.0/1.2 API 差异                     |
| MQTT       | `mqtt`                               | libmosquitto 2.0.21           | ✅ 已实现     | 常驻订阅 + 消息新鲜度判定                                           |
| WebSocket  | `web_socket`（`websocket` 别名兼容） | 自研（替代 Drogon WS Client） | ⚠ **需重写** | 原实现依赖 `drogon::WebSocketClient`，扩展形态不能带 Drogon，见 1.3 |
| Socket TCP | `socket_tcp`                         | 裸 socket                     | ✅ 已实现     | `parse_rule` 报文解析                                               |
| Socket UDP | `socket_udp`                         | 裸 socket                     | ✅ 已实现     | 同上                                                                |

### 1.2 已具备的基础设施（可直接迁移到扩展）

> ⚠️ **本表"旧位置"列指的是旧 Drogon 工程（`01-IBMCP`）里的文件名，不是本仓库 `collector-cpp/src/` 的现状。**
> 本仓库当前已落地的 C++ 源文件只有 10 个（`module.cpp` / `collector.cpp` / `timewheel.cpp` / `io_pool.cpp` /
> `data_store.cpp` / `plan_parser.cpp` / `plan_validator.cpp` / `event_emitter.cpp` / `logger.cpp` /
> `driver_factory.cpp`），且命名统一为 **snake_case**。表中 `BaseDriver.h`、`ModbusDriver.cpp`、`ModbusCodec.h`、
> `CollectorService.cpp` 等属于**待迁移的目标文件**，尚未进入本仓库——按第七章 CMake 清单为准，不要照本表去找文件。

| 能力                                                                           | 旧位置（旧 Drogon 工程）                 | 迁移到扩展后的形态                                                |
|--------------------------------------------------------------------------------|------------------------------------------|-------------------------------------------------------------------|
| 驱动抽象接口 `IDeviceDriver`                                                   | `include/drivers/IDeviceDriver.h`        | **保留**，`DriverPoint` 字段对齐 plan                             |
| 公共基类 `BaseDriver`（连接生命周期 + 换算）                                   | `BaseDriver.h`                           | **保留**，`toEngineValue` / `toRawValue` 原样                     |
| `DriverFactory` 三套接口（`create(int)` / `createByName` / `listAvailable()`） | `DriverFactory.cpp`                      | **保留**，`listAvailable()` 直接支撑 `Collector.list_protocols()` |
| Modbus 地址编解码                                                              | `ModbusCodec.h`                          | **保留**                                                          |
| 批量读合并（贪心 + 三个 constexpr 上限）                                       | `ModbusDriver.cpp`                       | **保留**，阈值改为可从 plan 覆盖                                  |
| 周期重载 / 驱动池复用 / 设备级互斥                                             | `CollectorService.cpp`                   | **重写**（见第三章）                                              |
| `scale`/`offset` 统一换算（收敛到基类）                                        | `BaseDriver.cpp`                         | **保留**，且只做一次                                              |
| 启动自检 WARN                                                                  | `CollectorService::startupSelfCheck()`   | 改为 `Collector.health()` 返回的 `warnings[]`                     |
| `using_devices.status` 回写                                                    | `CollectorService::updateDeviceStatus()` | **移除**（DB 访问全归 Python，见 2.2）                            |

### 1.3 WebSocket 驱动必须重写（扩展形态的硬约束）

旧 `WebSocketDriver` 用 `drogon::WebSocketClient`，这意味着**整个 Drogon 框架（含 Trantor 事件循环）都要打进扩展**。而扩展要主动调
Python 回调、又要在 Python 进程里跑，两套事件循环（Trantor + asyncio/uvloop）混在一起会引发：

- Trantor 的 `EventLoopThread` 与 Python 的 GIL 抢锁，回调穿越时可能死锁；
- Drogon 全局有单例与静态初始化，作为扩展模块被解释器卸载/重载时易崩；
- 体积从 ~200KB 涨到 ~20MB（静态链接 Drogon 的话）。

**结论：扩展不引入 Drogon/Trantor。** `web_socket` 驱动改为：

| 方案     | 实现                                                                                                       | 取舍                                   |
|----------|------------------------------------------------------------------------------------------------------------|----------------------------------------|
| **推荐** | 用 `libwebsockets`（openEuler 源有 `libwebsockets-devel`）或轻量自研（`socket` + 最小握手 + `frame` 解析） | 体积小、无额外事件循环，跑在驱动线程里 |
| 备选     | 把设备侧 WS 采集**降级为不可用**，protocol 清单不含 `web_socket`                                           | 若现场无 WS 设备，直接砍掉最省事       |

> 决策建议：**v1 用轻量自研 WS 客户端**（只需支持"连 ws://、发 subscribe 报文、收文本帧、按 `parse_rule` 解析"这几件事，不涉及
> TLS 时约 300 行）。这比引入 libwebsockets 更可控，也不用担心它的 lifecycle 与 GIL 交互。
>
> 若必须支持 `wss://`，再引 libwebsockets（它自带 TLS 后端集成）。

### 1.4 前端的 WS 与设备的 WS 是两条完全不同的链路（保留这个区分）

| 链路                                  | 旧实现                                  | 新实现                                   |
|---------------------------------------|-----------------------------------------|------------------------------------------|
| **后端 → 浏览器**（推实时数据）       | `IbmcpWsController`（Drogon 服务端 WS） | **FastAPI 的 `/api/v1/ws/collect`**，与扩展无关 |
| **后端 → 设备**（采集设备推送的数据） | `WebSocketDriver`（Drogon 客户端）      | **扩展内的 `web_socket` 驱动**           |

> 02.02 号前端文档里"四固定主题"指的是**上表第一条**，和采集扩展毫无关系。开发时若把两件事混为一谈，必然导致"要给扩展加 WS
> 服务端"的错误结论。

---

## 二、扩展形态带来的四条根本性改变（必须先理解，否则会照旧代码改错）

### 2.1 线程所有权：扩展的线程不是 Drogon 的线程

| 维度         | 旧（Drogon 进程内）                                                  | 新（扩展，被 Python import）                             |
|--------------|----------------------------------------------------------------------|----------------------------------------------------------|
| 线程创建者   | `CollectorService::start()` 用 `std::thread`，由 Drogon 启动钩子触发 | 由 `Collector.__init__` 创建，**Python 决定何时创建**    |
| 线程生命周期 | 与 Drogon 进程同生共死                                               | 与 **Python 进程**同生共死；`shutdown()` 必须能干净 join |
| 线程数量     | 每设备 1 线程（N+1）                                                 | **固定大小 IO 线程池**（见 3.2），与设备数解耦           |
| 事件循环     | Drogon 的 Trantor loop                                               | **扩展自己的 loop，与 asyncio 完全隔离**                 |
| 谁调用驱动   | CollectionService 的 deviceLoop 线程                                 | 线程池的 worker 线程                                     |

**红线**：扩展**禁止**创建任何依赖 Python 的线程逻辑（不能在采集线程里长时间持 GIL 做 Python 调用）。反过来，扩展线程**可以**
调 Python 回调，但必须走 `py::gil_scoped_acquire` + `loop.call_soon_threadsafe(queue.put_nowait, obj)` 的"只入队"模式（见
05 号 3.3 与 6.2）。

### 2.2 数据访问：扩展不写数据库

旧 `CollectorService` 直接 `DataService::ingest(row)` 写库、`updateDeviceStatus()` 写状态。扩展形态下**全部移除**：

| 旧行为                                           | 新归属                                                        |
|--------------------------------------------------|---------------------------------------------------------------|
| 写 `time_series_data`                            | Python `SamplePump` 每 200ms `snapshot_since()` 拉批 → `COPY` |
| 写 `using_devices.status`                        | Python 消费 `device_status` 事件后写 `using_device_runtime`   |
| 读 `using_devices` / `using_points` 获取采集配置 | **改为从 plan 取**（`apply_plan` 传入）                       |
| 周期重查 `updated_at` 检测配置变化               | **改为 Python 调 `apply_plan`**（增量/全量）                  |
| 报警判定 `checkAndFireAlarm`                     | Python 内存态判定（阈值已在 plan 里）                         |

**收益**：扩展**零数据库依赖** —— 不链 `libpq`、不含 SQL、不关心 schema。带来三个工程好处：

1. 扩展可以单独做单元测试（喂 plan、取快照，无需 PG）；
2. Windows 侧 stub 实现能 100% 模拟真扩展行为；
3. 数据库 schema 变更永远不会波及 C++ 编译。

> 这也是"扩展形态优于独立进程"在工程上的实际回报：独立进程仍然要自己连 PG，就享受不到这些。

### 2.3 配置来源：唯一入口是 `apply_plan`

旧版采集源是 `using_devices` + `using_points` 两张表，还伴随"触发器没建就读空表、安静不采"的隐患（04 号文档 7.2）。新版：

```
运行配置（running_*） ──plan_compiler──► Plan dict ──apply_plan──► 扩展内存
```

扩展内存中的结构就是**它此刻唯一的真相**：

```cpp
struct DeviceRuntime {
    std::string device_key;        // = device_code
    std::string protocol;          // 规范名
    Json::Value connection;        // 原样透传
    std::vector<PlanPoint> points;
    DriverPtr driver;              // 懒创建
    std::mutex ioMutex;            // 单设备 IO 串行
    std::atomic<bool> online{false};
    std::string last_error;
};

struct PlanPoint {
    int64_t point_id;              // = running_device_collection.id，用于回传
    std::string address;
    std::string data_type;
    double scale{1.0}, offset{0.0};
    int cycle_ms{1000};
    double alarm_high{0.0}, alarm_low{0.0};
    bool alarm_enabled{false};
    std::string read_write{"R"};
    std::string unit;
};
```

**"读空表安静不采"的隐患消除**：`apply_plan` 若 plan 为空，扩展会在 `health()` 中显式报 `devices=0`，且 `plan_ack` 事件明确回传
`device_count=0`。Python 侧 `/api/v1/collector/health` 一看就知道"是没配置，不是采不到"。

### 2.4 错误传播：从"日志"到"返回值 + 事件"

旧版错误处理靠 `LogError` 打印，上层无感知 —— 这正是"设备连不上但前端显示在线"这类问题的根源。扩展形态必须有
**结构化错误通道**：

| 错误类型                                            | 传播方式                                                                                                                                                                                                               |
|-----------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **构造失败**（参数非法、扩展版本不匹配）            | `init()` 抛 `NotInitialized(rc=4)` / `InvalidConnection(rc=2)`                                                                                                                                                         |
| **plan 非法**（协议不支持、连接参数缺失、地址为空） | `apply_plan` 抛 `ProtocolNotSupported(rc=1)` / `InvalidConnection(rc=2)`，**带具体 device/point 定位**。**预览校验不跨边界** —— 由 Python 侧 `plan_compiler` 复刻同一套规则（见 05 号 8 接口清单，无 `validate_plan`） |
| **运行期连接失败**                                  | `device_status` 事件（`status=0` + `reason`）                                                                                                                                                                          |
| **运行期单点读失败**                                | `snapshot_since` 返回项中该点 `quality=0` + `error`，**不影响同批其他点**                                                                                                                                              |
| **运行期写失败**                                    | `write_result` 事件（`success=false` + `message`）                                                                                                                                                                     |
| **扩展内部异常**（未捕获 C++ 异常）                 | 绝不允许逃逸到 Python 调用方，必须在外层 `catch` 并转 6 类注册异常（见 01 号 3.3 C1 与 05 号 8.1）                                                                                                                     |

**异常对象的强制结构**（结构定义以 05 号 8.1 为唯一权威，此处仅列字段）：

```python
# 扩展抛出后，Python 侧持有的实例（rc 由 C++ 填，code 由 Python 派生）
InvalidConnection(
    rc=2, stage="apply_plan",
    device_key="DEV-001", point_id=0,
    message="connection missing key: host",
)   # → HTTP code = 4000 + rc = 4002
```

> **`rc` 在异常实例上，`code` 不在**：`code` 是 Python 侧的派生量（`4000 + rc`），由 `to_api_error()` 计算，C++ 侧不感知绝对码。少了
> `device_key` / `point_id` 这两个定位字段，运维面对"4002 连接参数非法"只能一台台试——这是本文档对旧版
> `validateParams(std::string &err)` 最实质的加强。

---

## 三、线程与调度模型（扩展 v1 的核心设计）

### 3.1 为什么放弃"每设备一线程"

旧版（04 号文档 7.3 隐患 3 的副作用明确写了）：**设备数 N 时线程数 N+1**，超过数百设备必须改。新版目标（主文档 11.1）是 5000
点位、高并发，若按 500 台设备算就是 500 线程 + 500 个 socket —— 上下文切换开销比 IO 本身还大。

### 3.2 时间轮 + 固定 IO 线程池

```
┌──────────────────────────────────────────────────────┐
│  Timer Thread（1 个，仅做调度，绝不 IO）               │
│  时间轮 tick = 50ms，bucket = 1000/50 = 20 档（1s）    │
│  每个 tick：把到期的 device 任务推入 task_queue        │
└───────────────────────┬──────────────────────────────┘
                        │ push
                        ▼
┌──────────────────────────────────────────────────────┐
│  Task Queue（有界 MPMC，容量 = 设备数 × 2）            │
└───────────────────────┬──────────────────────────────┘
                        │ pop
                        ▼
┌──────────────────────────────────────────────────────┐
│  IO Thread Pool（N = max(4, cores)，可配 4~32）        │
│   worker：取 task → ioMutex 加锁 → driver.readPoints() │
│           → 解锁 → 写入 DataStore 快照（不回调）        │
└──────────────────────────────────────────────────────┘
```

> **注意**：读路径**不回调 Python**，worker 只把结果写入 `DataStore` 的无锁快照缓冲；Python 侧由 `SamplePump` 定时
> `snapshot_since()` 拉取。回调只用于低频状态事件（`device_status` / `write_result` / `plan_ack`）。原因见 05 号 3.3（避免每点一次
> GIL 切换）。

**关键决策与理由**

| 决策         | 值                                                                   | 理由                                          |
|--------------|----------------------------------------------------------------------|-----------------------------------------------|
| tick 粒度    | 50ms                                                                 | 采集最快周期 100ms 也能表达；过小则时间轮空转 |
| 设备任务粒度 | **整台设备一次**（含全部点位）                                       | 复用一条连接、Modbus 可合并帧；不按点位调度   |
| 同一设备并发 | **永不为 1 以上**（`ioMutex` + 时间轮不重复入队）                    | 保 Modbus 事务 ID 与 RTU 总线串行             |
| 不同设备并发 | 池大小内并行                                                         | 单设备超时不阻塞其他设备                      |
| 池大小       | 默认 `min(32, cores*2)`，可由 `IBMCP_KERNEL_IO_THREADS` 环境变量覆盖 | IO 型任务，可适当超配                         |
| 慢设备处理   | 超时后该设备任务**丢弃本周期**，不补跑                               | 避免积压；下一周期自然重试                    |
| 快照写入     | **worker 线程直接写 DataStore**，无锁 CAS/双缓冲                     | 不额外起分发线程，减少一跳延迟                |

**与旧版的两阶段模型（04 号文档 7.4）的关系**：完全保留"锁内决策、锁外执行"原则 —— 池 worker 取任务时在 plan 锁内只拿
`DeviceRuntime*` 与点位快照（`shared_ptr`），释放 plan 锁后再 `ioMutex.lock()` 做 IO。**`plan 锁` 与 `ioMutex`
绝不嵌套持有**，这是旧版用血换来的红线。

### 3.3 采集周期的三种表达

| 场景                      | 表达方式                                                                            |
|---------------------------|-------------------------------------------------------------------------------------|
| 全设备统一周期            | `cycle_ms` 全部相同 → 时间轮每周期只入队一次                                        |
| 点位周期不同              | device 任务按**最小周期**触发，内部按 `nextDue[i]` 判断本轮到期的点（跳过未到期点） |
| 极慢点位（如 1 小时一次） | 走**独立慢轮**（tick 1s，档 3600），不进主轮                                        |

> **不要**为每个点位建独立定时器。5000 点位就是 5000 个定时器，管理成本远超收益。按设备聚合 + 内部到期判断是最优解。

### 3.4 驱动池与连接复用

```
DeviceRuntime.driver 懒创建：
  if (!driver || !driver->isConnected() || connection 变更)
      → DriverFactory::createByName(protocol) → setConnectionParams → validateParams → connect
  connect 失败 → device_status(offline, reason) → 标记退避（指数 1s/2s/4s… 上限 60s）
```

| 规则     | 说明                                                                                    |
|----------|-----------------------------------------------------------------------------------------|
| 连接复用 | 控制写与采集读**共用同一个 driver**（旧版 `CollectorService::writePoint()` 的语义保留） |
| 连接失效 | `isConnected()` 为 false 时下次任务重建；不主动探活（省一次往返）                       |
| 退避     | 连不上的设备按指数退避，避免对故障设备每秒重连                                          |
| 驱动析构 | `apply_plan` 移除设备时**在 plan 锁外** disconnect + 析构（避免析构中触发 IO 卡住全局） |

### 3.5 控制写入路径（与采集的关系）

```
Python: collector.write_point(device_key, point, command_id) ──┐
                                                              ▼
                          写入 control_queue（有界，容量 1024）
                                       ▼
                     IO Pool 的写通道（独立于采集任务）
                                       ▼
              ioMutex.lock() → driver.writePoint(raw) → unlock
                                       ▼
              write_result 事件 → Python
```

| 要点              | 说明                                                                 |
|-------------------|----------------------------------------------------------------------|
| **入队即返回**    | `write_point` 不做任何 IO，p99 < 30ms（主文档 11.1 指标）            |
| **与采集同锁**    | 写必须与读抢同一把 `ioMutex`，保证报文不交错（Modbus 红线）          |
| **优先级**        | 写任务优先于采集任务出队（控制是人工操作，延迟敏感）                 |
| **超时**          | 队列等待 + 执行总耗时 > 5s → `write_result(success=false, status=4)` |
| **设备不在 plan** | 立即返回 4004 + `device not in plan`，不尝试建临时连接               |

> **这是与旧版最大的行为差异**：旧版 `DeviceController::control` 的兜底路径是"不在采集计划里也能
> `createByName → connect → write → disconnect`"。扩展形态下**禁止**这个兜底 —— 它会在扩展里造出计划外的连接，绕过所有并发约束。控制只能作用于已下发设备，这是硬约束。

---

## 四、驱动契约（`IDeviceDriver` 扩展版）

### 4.1 接口签名（保留旧版 + 两处强化）

```cpp
class IDeviceDriver {
public:
    virtual ~IDeviceDriver() = default;

    virtual const char *name() const = 0;                 // 规范名，如 "modbus_tcp"
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual bool readOne(const DriverPoint &pt, ReadResult &out) = 0;
    virtual void readPoints(const std::vector<DriverPoint> &pts, BatchReadCallback cb) = 0;
    virtual bool writePoint(const DriverPoint &pt, const std::string &value, std::string &err) = 0;

    virtual bool validateParams(std::string &err) const = 0;   // 强化①：必填+范围校验
    virtual void setConnectionParams(const Json::Value &p) = 0;

    // 强化②：能力声明（新增）
    virtual DriverCapability capability() const = 0;
    struct DriverCapability {
        bool supportBatchRead;      // Modbus=true，其余=false
        bool supportSubScribe;      // MQTT=true（常驻订阅）
        bool supportWrite;
        int  maxPointsPerFrame;     // Modbus=125/2000，其余 0=不限
    };
};
```

**强化①的原因**：旧版 `validateParams` 是"能检查多少检查多少"，实际只检查了部分键。扩展版要求
**每个协议必须声明必填键清单并全部校验**，因为 `apply_plan` 要一次性拒绝所有非法配置（Python 侧 02 号文档 6.2 的校验表就是它的镜像）。

**强化②的原因**：`Collector.list_protocols()` / `protocol_capabilities()` 要返回 `supports_write`
等信息供前端置灰。旧版没有能力查询，前端只能"全都可选、报错再改"。

### 4.2 `DriverPoint` 字段（与 plan 对齐）

```cpp
struct DriverPoint {
    int64_t     point_id{0};
    std::string address;      // Modbus 40001 / OPC UA ns=3;s=Temp / MQTT topic
    std::string data_type;    // bool/int/float/double/string
    double      scale{1.0};
    double      offset{0.0};
    int         cycle_ms{1000};      // 旧版是 cycle_sec，扩展版改毫秒（与 plan 一致）
    int         function_code{0};    // Modbus 专用，0=由地址推导
    int         register_count{0};   // Modbus 批量读长度
    std::string unit;
    Json::Value extra;               // 协议私有扩展
};
```

**`cycle_sec → cycle_ms` 的单位变更是本次唯一的字段语义改动**，原因：plan 统一毫秒（主文档 7.2），驱动内部若再用秒会引入两套单位；且
100ms 级采集周期用秒表达不了。

> **命名红线**：`DriverPoint` 的字段名与 plan JSON 键名**逐字一致**（全蛇形），`module.cpp` 里 `d["cycle_ms"].cast<int>()`
> 直接赋值，不做任何驼峰映射。

### 4.3 各协议连接参数契约（与 02 号文档 6.1 完全一致）

| 协议                        | 必填键                  | 可选键                                                                                                                  | 常见失败原因                                            |
|-----------------------------|-------------------------|-------------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------|
| `modbus_tcp`                | `host`, `port`          | `slave`(1)、`timeout_ms`(3000)、`batch_gap`(8)                                                                          | 端口写 0；PLC 侧无访问规则                              |
| `modbus_rtu`                | `device`                | `baud`(9600)、`parity`(N)、`data_bits`(8)、`stop_bits`(1)、`slave`(1)                                                   | `/dev/ttyUSB0` 不存在；用户不在 `dialout` 组            |
| `opc_ua`                    | `endpoint`              | `security_policy`(None)、`username`、`password`、`timeout_ms`                                                           | endpoint 未以 `opc.tcp://` 开头                         |
| `mqtt`                      | `host`, `port`          | `client_id`、`username`、`password`、`qos`(1)、`keep_alive`(60)、`subscribe_wait_ms`(1500)、`max_message_age_ms`(30000) | `client_id` 多实例重复；上报周期 > `max_message_age_ms` |
| `web_socket`                | `url`                   | `subscribe_msg`、`parse_rule`、`timeout_ms`                                                                             | url 未以 `ws://` 开头                                   |
| `socket_tcp` / `socket_udp` | `host`, `port`, `proto` | `send_hex`、`parse_rule`、`timeout_ms`                                                                                  | `parse_rule` 缺失 → 解析不出值                          |

> **`batch_gap` 是本次新增的可配项**：旧版 `kMaxBlockGap=8` 是 `constexpr` 硬编码（前端/现场多次反馈"若现场地址分散可考虑改为从
> connection 读取，当前需改代码"）。扩展版**直接做成连接参数**，默认仍为 8，`kMaxBlockRegisters=125` / `kMaxBlockBits=2000`
> **仍是协议硬上限，不给配**。

### 4.4 地址语义（各协议解析规则，保留旧版结论）

| 协议               | 地址形态                                                                  | 解析器                                                              |
|--------------------|---------------------------------------------------------------------------|---------------------------------------------------------------------|
| Modbus             | `0xxxx` 线圈 / `1xxxx` 离散输入 / `3xxxx` 输入寄存器 / `4xxxx` 保持寄存器 | `ModbusCodec`：地址 → 功能码 + 偏移 + 寄存器数（由 `data_type` 推） |
| OPC UA             | NodeId 字符串 `ns=3;s=Temperature1`                                       | 直接透传                                                            |
| MQTT               | topic 字符串                                                              | topic 即地址；**首次读才订阅**，后续读缓存                          |
| Socket / WebSocket | 报文模板 + `parse_rule`                                                   | 按规则解析字节流                                                    |

**Modbus 批量读合并算法（保留原实现，逻辑不变）**

```
1. 按 function_code 分组（0 则由地址前缀推）
2. 组内按起始偏移升序排列
3. 贪心：当前帧 [start, end]，下一个点的 start - end <= batch_gap 则并入
4. 帧长度超 kMaxBlockRegisters(125) / kMaxBlockBits(2000) 则开新帧
5. 每帧一次 IO，结果按点位偏移回填
```

> **为什么这是性能前提**：100 个点位逐点读 = 每秒 100 次往返，PLC 会被打挂（04 号文档 5.2 原话）。合并后通常 2~4 帧。

### 4.5 `scale` / `offset` 换算边界（三条不可动摇的规则）

```cpp
inline double toEngineValue(double raw, const DriverPoint &p) { return raw * p.scale + p.offset; }
inline double toRawValue(double engine, const DriverPoint &p) {
    return p.scale != 0.0 ? (engine - p.offset) / p.scale : engine;
}
```

| 规则                              | 说明                                                                                               |
|-----------------------------------|----------------------------------------------------------------------------------------------------|
| ① 读路径**只换算一次**            | `readOne` 成功后立刻 `toEngineValue`，原始值**不出驱动层**（入库与推送都是工程值）                 |
| ② 写路径**自动逆换算**            | `writePoint` 内部构造 `scale=1,offset=0` 的副本再用 `toRawValue`，避免二次污染                     |
| ③ **`scale != 0` 是唯一安全边界** | 为 0 时 `toRawValue` 直接返回 engine（不除零），但读侧会恒等于 `offset` → **该点所有读数完全相同** |

> **`scale=0` 的表现极具迷惑性**：没有报错、没有 Bad 质量码、曲线是一条水平直线。旧版靠"建表默认 1.0 + 读时 NULL 兜底
> 1.0"防，但防不住人工填 0。
> **扩展版防法**：`apply_plan` 校验 `scale != 0`，非 0 才进内存，compiler 侧 Pydantic 也拒（02 号文档 3.6）。
> **双重拦截，任何一路漏掉都没事**。

### 4.6 质量码（Quality）约定

| 值          | 产生场景                                                                                             |
|-------------|------------------------------------------------------------------------------------------------------|
| `Good`      | 读取成功且通过新鲜度判定                                                                             |
| `Bad`       | 连接失败 / 协议异常 / MQTT 消息陈旧 / 解析失败（此时 `value` 保留上一次值或 0，由 `error` 字段说明） |
| `Uncertain` | 设备返回部分有效（如 OPC UA status code 非 Good 但可读）                                             |

**报警判定必须跳过 `Bad`**（02 号文档 5.4 规则②），否则一次断链会让所有报警点位同时触发，导致报警风暴。

---

## 五、关键运行期行为（扩展必须保证的 6 条）

### 5.1 单设备故障隔离

| 故障                                 | 影响范围                     | 扩展行为                                                  |
|--------------------------------------|------------------------------|-----------------------------------------------------------|
| 单点地址非法                         | 该点 `Bad`                   | 同批其他点正常返回                                        |
| 单帧 Modbus 异常码（如 02 非法地址） | 该帧内点位 `Bad`，其他帧正常 | 记录 `last_error`，不影响设备 `online`                    |
| 连接断开                             | 该设备全部点位 `Bad`         | `device_status(offline)` + 指数退避重连                   |
| 单设备阻塞（socket 无响应）          | **仅该设备本周期**           | socket 超时（`timeout_ms`）到点返回；任务不补跑           |
| 扩展内部异常                         | 该次采集调用                 | 捕获后写 `ok=false` 进快照，**绝不外抛**（主文档 3.3 C1） |

### 5.2 事件回调负载保护（扩展形态特有，旧版没有）

扩展线程直接调 Python 回调，若 Python 侧处理慢（比如写 DB），会**反压到扩展线程**，进而拖慢所有设备。

```
扩展 worker ──回调──► Python _on_event
                         │
                         └─► 必须 < 1ms 返回（只做入队）

禁止在回调里：await DB / 写日志文件 / 加锁 / json.loads 大对象
```

| 保护机制       | 值                                                                                                        |
|----------------|-----------------------------------------------------------------------------------------------------------|
| 回调超时监测   | 扩展记录每次回调耗时，> 50ms 记 `slow_callback` 计数（写入 `health()` 的 `stats` 段）                     |
| 回调不可重入   | 扩展保证同一 worker 串行调用，但**不同 worker 会并发调 Python** → Python 侧回调必须是线程安全的纯入队操作 |
| 事件队列满策略 | Python 侧 `event_queue` 满时**丢弃最旧的低优先级事件**并记 `dropped`；**绝不阻塞回调**                    |
| 采样路径       | **不走回调**（`DataStore` 快照 + `snapshot_since` 拉取），从根上消除"每点一次回调"的反压风险              |

> **这条是扩展形态与独立进程最大的运维差异**：独立进程有 socket 缓冲天然解耦，扩展形态是**同进程直连**，Python
> 的卡顿会直接表现为采集周期抖动。因此"回调只入队"是硬性纪律；而采样完全不依赖回调，只靠快照拉取。

### 5.3 崩溃边界

| 崩溃点                    | 后果                                                                     | 兜底                                                                                                              |
|---------------------------|--------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------|
| 驱动内部 C++ 异常         | 若逃逸出 pybind11 绑定函数 → 默认 `std::terminate`，整个 Python 进程崩溃 | `try/catch(...)` 包住 `apply_plan` / 采集 worker / `write_point` 的**全部实现**，统一转 6 类注册异常（05 号 8.1） |
| 段错误（空指针 / 野指针） | **进程直接死**                                                           | 无法兜底；靠 systemd `Restart=always` + `runtime_plan_revision` 重放（主文档 7.7）                                |
| 扩展卸载时线程未 join     | 崩溃或泄漏                                                               | `shutdown()` 必须 join 全部线程后才允许返回；进程退出顺序由 lifespan 保证                                         |
| fork 后使用扩展           | 子进程里线程全没了                                                       | **禁止 fork**：`uvicorn` 不用 `--preload`，`Collector` 构造放在 `lifespan`（fork 之后）                           |

### 5.4 线程数与资源上界（可配置，必须校验）

| 资源         | 默认               | 上界     | 超限行为                                                                                                      |
|--------------|--------------------|----------|---------------------------------------------------------------------------------------------------------------|
| IO 线程      | `min(32, cores*2)` | 64       | `init` 直接抛 `InvalidConnection(rc=2, stage="init", message="io_threads too large")` → `code=4002`           |
| 单帧最大点位 | Modbus 125/2000    | 协议上限 | 合并算法自动拆帧，不拒绝                                                                                      |
| 控制队列     | 1024               | 8192     | `write_point` 抛 `ControlQueueFull(rc=5)` `queue full` → `code=4005`                                          |
| 单设备点位数 | 不限               | 65535    | `apply_plan` 拒收 `InvalidConnection(rc=2, message="too many points")` → `code=4002`（防内存爆）              |
| 总设备数     | 不限               | 2000     | `apply_plan` 拒收 `InvalidConnection(rc=2, message="too many devices")` → `code=4002`（超过则需分多实例部署） |

> `rc` 与 `code` 的换算关系见 05 号 8.1：`rc` 是扩展侧相对码（1~6），`code = 4000 + rc` 由 Python 派生。**扩展代码里只写 `rc`
> ，绝不写 4001/4006 这类绝对码**。

### 5.5 时间与时钟

| 项              | 规则                                                                                                                  |
|-----------------|-----------------------------------------------------------------------------------------------------------------------|
| 采样时间戳 `ts` | **扩展取 `steady_clock` 的采样时刻**，转 Unix 毫秒                                                                    |
| 时区            | 扩展只产出**UTC 毫秒**，不做本地时区转换（Python 侧落库也不转，展示层转）                                             |
| 时钟跳变        | 用 `steady_clock` 做周期调度（不受 NTP 跳变影响）；`ts` 用 `system_clock`（会受影响，符合"数据带真实时刻"的业务要求） |
| 时间戳精度      | **毫秒**（旧版是秒，普通表 `ts` 是 TIMESTAMPTZ，毫秒能落进同一秒内区分）                                              |

> **毫秒化带来的唯一风险**：旧版去重键是 `(ts秒, point_id)`
> ，改毫秒后同一秒内会产生多个记录。若按秒做聚合取整会看到"一秒多条"。这是**符合"高频采集"业务需求**的，但必须在 DB 侧确认连续聚合的
> `date_trunc` 时间边界对齐策略正确，否则桶边界会漂移。

### 5.6 日志

| 项                   | 规则                                                                           |
|----------------------|--------------------------------------------------------------------------------|
| 扩展日志**不写文件** | 通过构造参数中的 `log_cb` 回传给 Python，由 Python 统一走 structlog            |
| 日志级别             | 由 `cfg["log_level"]` 控制（`error/warn/info/debug`）                          |
| 禁止项               | **不允许扩展用 `printf` / `std::cout` 直写 stdout** —— 会污染 uvicorn 的输出流 |
| 高频日志抑制         | 同一设备的连接失败日志**首次 + 每 60s 一次**，不按周期刷屏                     |

---

## 六、驱动能力查询（支撑 `/api/v1/collector/protocols`）

```python
collector.list_protocols() -> list[str]
# ["modbus_tcp", "modbus_rtu", "opc_ua", "mqtt", "web_socket", "socket_tcp", "socket_udp"]

collector.protocol_capabilities() -> dict[str, dict]   # 可选扩展接口
# {"modbus_tcp": {"supports_read": True, "supports_write": True, "supports_batch_read": True}, ...}
```

**实现来源**：`DriverFactory::listAvailable()` + 各驱动的 `capability()`。宏隔离（`IBMCP_HAS_MODBUS` 等）为 OFF 的协议
**不出现在清单**，因此清单在**编译期**就决定了 —— 这正是"字典写了 PROFINET 但扩展没驱动"能被前端置灰的依据。

| 场景                       | `list_protocols()` 表现                               |
|----------------------------|-------------------------------------------------------|
| 全部宏 OFF（Windows stub） | 返回空列表 → 所有协议置灰，前端统一提示"扩展为桩实现" |
| 只开 MODBUS                | 只返回 `modbus_tcp` / `modbus_rtu`                    |
| 全部 ON                    | 7 项（含 `web_socket`，若其驱动已实现）               |

> **DDL 层面的连带影响**：02 号文档 3.5 提到字典有 11 项。真实可用的只有清单里的；
> `PROFINET / PROFIBUS_DP / CCLINK / S7_COMM` 四项**永远不可能出现在清单**（无实现计划），`PROTOCOL_CODE` 保留仅作历史兼容。

---

## 七、CMake 与依赖（openEuler 侧，Windows 侧不参与）

### 7.1 三套工业库的实测结论（可直接沿用）

| 库                  | 包名（Debian 实测）    | openEuler 对应包                    | 头文件包含方式                                 | 链接                                      |
|---------------------|------------------------|-------------------------------------|------------------------------------------------|-------------------------------------------|
| libmodbus 3.1.11    | `libmodbus-dev`        | `libmodbus-devel`                   | **`#include <modbus/modbus.h>`**（注意子目录） | `-lmodbus`                                |
| open62541 1.4.11    | `libopen62541-1.4-dev` | `open62541-devel`（需确认源里版本） | `#include <open62541/client_config_default.h>` | `find_package(open62541 CONFIG REQUIRED)` |
| libmosquitto 2.0.21 | `libmosquitto-dev`     | `libmosquitto-devel`                | `#include <mosquitto.h>`                       | `-lmosquitto`                             |

### 7.2 三个必须注意的点（旧文档实测结论，仍然有效）

**① libmodbus 头文件在子目录里**

```
/usr/include/modbus/modbus.h        ← 实际路径
#include <modbus/modbus.h>          ✅
#include <modbus.h>                 ❌ 找不到
```

`libmodbus.pc` 带 `-I${includedir}/modbus`，走 pkg-config 两种写法都过；**本项目 CMake 手写 `target_link_libraries` 不走
pkg-config，所以必须写子目录形式**。

**② mosquitto 必须装 dev 包，且 pkg-config 模块名是 `libmosquitto`**

```
apt install -y libmosquitto-dev          # 不是 mosquitto-dev（那是 broker 插件开发包）
pkg-config --modversion libmosquitto     ✅ 2.0.21
pkg-config --modversion mosquitto        ❌ NOT FOUND
```

**③ open62541 用 1.4，且 API 与 1.0/1.2 有差异**

- 1.4 提供 `open62541Config.cmake`，**不必源码编译**（旧文档"建议源码编译"的结论应更正）；
- `UA_Client_connect` 返回值、`UA_Client_getState` 等有变化，**写驱动必须对照 1.4 头文件**，不要照抄网上 1.0 示例。

### 7.3 CMakeLists 骨架（扩展模块，**已按仓库实测对齐**）

```cmake
cmake_minimum_required(VERSION 3.20)
project(ibmcp_collector LANGUAGES CXX)     # 实测工程名

set(CMAKE_CXX_STANDARD 17)                 # 实测用 17，非 20（std::thread 足够；用 std::jthread 才需 20）
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(pybind11 CONFIG REQUIRED)

# 协议库用 pkg-config 探测：找到才启用，缺库降级为 OFF，不中断构建
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MODBUS QUIET libmodbus)
    pkg_check_modules(MOSQUITTO QUIET libmosquitto)
    pkg_check_modules(OPEN62541 QUIET open62541)
endif()

# 文件名统一 snake_case（实测 src/drivers/driver_factory.cpp，不是 DriverFactory.cpp）
set(COLLECTOR_SOURCES
    src/bindings/module.cpp        # 唯一的 pybind11 导出点
    src/collector.cpp
    src/scheduler/timewheel.cpp
    src/scheduler/io_pool.cpp
    src/store/data_store.cpp
    src/plan/plan_parser.cpp
    src/plan/plan_validator.cpp
    src/common/event_emitter.cpp
    src/common/logger.cpp
    src/drivers/driver_factory.cpp
)

pybind11_add_module(_core ${COLLECTOR_SOURCES})
target_include_directories(_core PRIVATE include)

if(MODBUS_FOUND)
    target_compile_definitions(_core PRIVATE IBMCP_HAS_MODBUS)
    target_include_directories(_core PRIVATE ${MODBUS_INCLUDE_DIRS})
    target_link_libraries(_core PRIVATE ${MODBUS_LIBRARIES})
endif()
if(MOSQUITTO_FOUND)
    target_compile_definitions(_core PRIVATE IBMCP_HAS_MQTT)
    target_include_directories(_core PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
    target_link_libraries(_core PRIVATE ${MOSQUITTO_LIBRARIES})
endif()
if(OPEN62541_FOUND)
    target_compile_definitions(_core PRIVATE IBMCP_HAS_OPCUA)
    target_include_directories(_core PRIVATE ${OPEN62541_INCLUDE_DIRS})
    target_link_libraries(_core PRIVATE ${OPEN62541_LIBRARIES})
endif()

target_compile_options(_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

# 相对路径，靠 build.sh 的 `cmake --install --prefix collector-python` 落到 collector-python/industrial_collector/
install(TARGETS _core LIBRARY DESTINATION industrial_collector)
```

> **与 05 号 7.1 的关系**：本节是**驱动侧视角的 CMake 骨架**，05 号 7.1 是**构建链路视角**的同一份文件。两处编译参数必须
> 逐字一致；若只改一处会得到"驱动文档说 C++20、实际编译 C++17"这类隐性偏差。**以仓库 `collector-cpp/CMakeLists.txt` 为准**。
>
> **`pybind11_add_module(_core ${COLLECTOR_SOURCES})` 不要写 `MODULE` 关键字**：`pybind11_add_module` 默认已生成 MODULE
> 库，显式传 `MODULE` 是合法但易与 `SHARED` 混淆，实测文件也未写。

> **`-fno-exceptions` 禁用**：pybind11 依赖异常把 C++ 错误转成 Python 异常，全局关异常会让 `py::register_exception` 与
> `throw` 全部失效。**推荐做法**：保留异常，在每个对外函数（`module.cpp` 中的 lambda/方法）外层加
> `try { } catch (const std::exception &e) { throw CollectorError(...); } catch (...) { throw CollectorError(...); }`
> 。这样扩展内可用异常、边界绝不泄漏（见 05 号 4.2）。

> **协议探测方式已统一**：上面 7.3 的 `pkg_check_modules(... QUIET)` + `if(MODBUS_FOUND)` 就是实测实现，
> **不再需要**旧稿里的 `option(IBMCP_ENABLE_* OFF)` 开关 + `find_path/find_library` 手写探测。两者的行为差异：
> `option` 默认 OFF 时驱动被静默排除（编出来的 `.so` 少了 modbus 能力却不报警）；`pkg_check_modules QUIET` 是
> "装了就自动启用、没装就降级"，更符合"能编就带上能力"的预期。**若需人为关闭某协议**，用 `-DCMAKE_DISABLE_FIND_PACKAGE_*`
> 或在 venv 里卸掉对应 `-devel` 包，不要再引入 `option`。

源码用宏隔离：

```cpp
#if IBMCP_HAS_MODBUS
#include <modbus/modbus.h>
#endif
```

> **这条宏隔离是 Windows 侧能跑业务的关键**（Windows 侧不构建扩展、走 stub，但保留此写法可让 IDE 索引不报红，且未来若需
> Windows 本地跑真扩展也直接可用）。

### 7.4 构建与安装（由 scikit-build-core 接管）

```toml
# pyproject.toml 片段（完整版见 05 号 5.1）
[tool.scikit-build]
cmake.source-dir = "collector-cpp"
wheel.packages = ["collector-python/industrial_collector"]
build-dir = "build/{wheel_tag}"
```

**安装即编译**：在项目根执行 `pip install .` 在欧拉侧一次完成"C++ 编译 + wheel 打包 + 站点安装"（`pyproject.toml` 在项目根，`backend/` 下无 `pyproject.toml`，故不要用 `pip install ./backend`），扩展落在
`site-packages/industrial_collector/_core*.so`，**不需要手工 `install()` 到 `/usr/local/lib`，也不需要
`LD_LIBRARY_PATH`** —— 这正是 pybind11 + scikit-build-core 相对 `add_library(SHARED) + ctypes.CDLL`
的最大工程收益（消除了"库找不到"这类部署故障的一半）。

> 若依赖的三方 `.so`（`libmodbus.so` 等）不在标准库路径，仍需在 systemd unit 里给 `LD_LIBRARY_PATH`；这部分属于**三方依赖**
> 而非本项目库，见 8.2 检查清单。

---

## 八、openEuler 运行环境清单（替代旧 Debian 清单）

> 旧文档 4.1 的 Debian 清单里，**A/B 两组大部分不再需要** —— 因为扩展不链 Drogon、不链 libpq、不连 DB。下表是 **FastAPI +
> 扩展** 的真实依赖。

### 8.1 构建期（仅欧拉侧，编译 `_core` 扩展）

| 包                                                     | 用途                            | 缺失后果                                |
|--------------------------------------------------------|---------------------------------|-----------------------------------------|
| `gcc-c++`（≥ 11，需 C++20）、`cmake`（≥ 3.20）、`make` | 工具链                          | 无法构建                                |
| `python3.11-devel`（仅头文件，`build.sh` 按需补装）     | pybind11 编译需要 Python 头文件 | `pybind11_add_module` 找不到 `Python.h`（**解释器用系统自带 python3.11，不单独装**） |
| `jsoncpp-devel`                                        | plan 解析 + 事件序列化          | 编译失败                                |
| `libmodbus-devel`                                      | Modbus TCP/RTU                  | 该协议 OFF                              |
| `libmosquitto-devel`                                   | MQTT                            | 该协议 OFF                              |
| `open62541-devel`                                      | OPC UA                          | 该协议 OFF                              |
| `libwebsockets-devel`（可选）                          | `wss://` 支持                   | 仅影响 TLS WS                           |
| `openssl-devel`                                        | 若 WS 需 TLS                    | 同上                                    |

> **旧清单里的 Drogon / Trantor / libpq-devel / libssl-dev / zlib / brotli / c-ares 全部不再需要**。这是去掉 Drogon
> 的直接收益：构建依赖从 12 个降到 6 个。

### 8.2 运行期系统层（FastAPI 侧）

| 项                            | 为什么必须                                     | 做法                                                                    |
|-------------------------------|------------------------------------------------|-------------------------------------------------------------------------|
| **PostgreSQL 15 服务**        | 全部配置与数据                                 | `systemctl enable --now postgresql`                                     |
| **PostgreSQL 15**             | `time_series_data` 普通表 + 时间索引           | 原生支持，无需额外扩展                                                  |
| **数据库 `device_db` 预创建** | init 脚本里的 `CREATE DATABASE` 被注释掉       | `createdb -E UTF8 device_db`                                            |
| **Redis 服务**                | 字典缓存、会话、限流                           | `systemctl enable --now redis-server`                                   |
| **Nginx**                     | 反代 + WS 升级 + 前端静态                      | 见主文档部署章节                                                        |
| **uvloop + httptools**        | FastAPI 高并发                                 | `pip install uvicorn[standard]`                                         |
| **时区 `Asia/Shanghai`**      | 前端展示一致                                   | `timedatectl set-timezone Asia/Shanghai`（**DB 存 UTC，只在展示层转**） |
| **`LimitNOFILE=65536`**       | 大量 socket + PG 连接池                        | systemd unit 里写                                                       |
| **`chrony` 时钟同步**         | 时间戳漂移会让普通表分区错位、聚合 bucket 错乱 | `systemctl enable --now chronyd`                                        |

### 8.3 采集功能特有（缺了能启动、采不到）

| 项                                            | 场景                                                                                                                                                            |
|-----------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `/dev/ttyUSB0` + `dialout` 组 + 宿主 USB 透传 | **仅 Modbus RTU**；无串口时 RTU 连不上，TCP 不受影响                                                                                                            |
| 设备网络可达                                  | 防火墙一般不拦出站，但设备侧 ACL 需放行本机 IP                                                                                                                  |
| **扩展模块可导入**                            | `python -c "import industrial_collector._core"` 必须成功；失败的典型原因是 Python 小版本与构建机不一致（ABI 不匹配）                                            |
| **`LD_LIBRARY_PATH`（仅三方库）**             | 若 `libmodbus.so` 等不在标准路径，`import _core` 会报 `libmodbus.so: cannot open shared object file`；本项目扩展自身不在此列（已随 wheel 安装到 site-packages） |

### 8.4 一条自检脚本（建议随部署交付）

```bash
#!/bin/bash
set -u
echo "== 扩展 =="
python3 - <<'PY'
from industrial_collector import Collector
c = Collector({"io_threads": 4})
print("✅ import OK, protocols:", c.list_protocols())
c.shutdown()
PY
echo "== 扩展依赖 =="
EXTSO=$(python3 -c "import industrial_collector._core as m; print(m.__file__)")
ldd "$EXTSO" | grep -E "not found" && echo "❌ 三方依赖缺失" || echo "✅ 依赖完整"
echo "== DB =="
psql -c "SELECT 1" || echo "❌ PostgreSQL 未安装"
echo "== Redis =="
redis-cli ping || echo "❌ redis 未启动"
echo "== 串口 =="
ls -l /dev/ttyUSB* 2>/dev/null || echo "（无串口，RTU 不可用）"
id -nG | grep -q dialout && echo "✅ dialout" || echo "⚠ 不在 dialout 组"
```

> `import industrial_collector` 是排查"扩展加载失败"的第一手段。报 `ModuleNotFoundError` 是没装；报
> `ImportError: ... undefined symbol` 或 `libxxx.so: cannot open shared object file` 是三方依赖缺失；报
> `ImportError: dynamic module does not define module export function` 是 Python 小版本 ABI 不匹配（需在构建机同版本 Python
> 下重编）。

---

## 九、开发与调试模式（Windows 编辑 + 欧拉构建）

### 9.1 职责边界（CLion Remote Host 模式）

| 侧         | 做什么                                                     | 不做什么                                                    |
|------------|------------------------------------------------------------|-------------------------------------------------------------|
| Windows 11 | 写 C++ 源码、跑 stub 实现下的 Python 业务与 pytest、调前端 | **不编译扩展**（无 libmodbus/open62541，无 C++20 完整支持） |
| openEuler  | `bash deployments/openeuler/build.sh` 编译 `_core`、跑真扩展、做协议联调 | 不写业务代码（编辑器在 Windows）                |

**连接方式**：

| 方式                       | 适用                                               |
|----------------------------|------------------------------------------------------|
| CLion Remote Host          | C++ 主开发，断点调试扩展（`gdbserver :1234` 附加）  |
| 欧拉终端（`dev-env.sh`）   | 编译、`native` 联调、查看日志（Python 调试在 Windows 本地） |

> **不需要 VS Code Remote-SSH / debugpy**：Python 侧在 Windows 本地用本地解释器调试（断点零网络延迟），
> 详见 08 号 §5.1。

### 9.2 远程构建配置（CLion 侧）

```
Deployment: SSH → /home/meng/ibmcp
CMake profile: Debug (Remote)，源目录指向 collector-cpp/
  -DIBMCP_ENABLE_MODBUS=ON
  -DIBMCP_ENABLE_MQTT=ON
  -DIBMCP_ENABLE_OPCUA=ON
Build dir:   /home/meng/ibmcp/collector-cpp/build
```

> **注意**：CLion 的 CMake profile 只用于**代码索引与调试**；正式构建路径统一走 `bash deployments/openeuler/build.sh`
> （内部用 CMake + pybind11 编译同一份 `CMakeLists.txt`，再 `pip install .` 把 `.so` 打进 venv）。两条路径共用同一个
> `collector-cpp/CMakeLists.txt`，避免"CLion 能编、build.sh 编不过"的口径分裂。

> **源码同步方向**：**Windows → 欧拉**（CLion 的 Upload）。欧拉侧**不要反向下拉**，否则 Windows 的 `CMakeCache.txt`
> 会被远端路径污染（旧文档 `cmake-build-debug-远程主机` 就是这个坑的产物，其 cache 里记着 `/home/meng/tmp/...`）。

### 9.3 扩展调试的三种手段

| 手段                  | 适用                          | 做法                                                                       |
|-----------------------|-------------------------------|----------------------------------------------------------------------------|
| **stub 实现**（首选） | Python 业务开发、CI、前端联调 | `app/core/collector/stub.py` 与真扩展同接口，返回合成数据                  |
| **gdb 附加**          | 扩展逻辑错误、崩溃定位        | 欧拉侧 `gdb -p $(pgrep -f uvicorn)`（需 `ptrace_scope` 放开）              |
| **协议交互日志**      | 现场设备不通                  | `connection.debug_hex=true` 时打印收发字节（**仅调试用，生产关闭**，量大） |

**stub 必须模拟的行为（否则单测无意义）**

| 行为                | stub 实现                                                                                              |
|---------------------|--------------------------------------------------------------------------------------------------------|
| `list_protocols()`  | 返回与真扩展相同的规范名清单（**从配置文件读，保证 CI 一致**）                                         |
| `apply_plan` 校验   | 完整复刻 `plan_validator` 的规则（协议白名单、必填键、`scale!=0`、地址非空）                           |
| `apply_plan` 异常码 | 与真扩展完全一致的 4001/4002/…                                                                         |
| 采样快照            | `snapshot_since()` 按 `cycle_ms` 生成增量项，**值用确定性公式**（`sin(t)` 或按 point_id 线性）便于断言 |
| 控制                | `write_point` 后投递 `write_result` 事件，按 `device_connection["mock_write_fail"]` 可模拟失败         |
| 崩溃模拟            | `cfg["simulate_crash_after"]=N` 时第 N 次快照后 `os._exit(1)`（**测重启重放链路**）                    |

> **stub 的价值被严重低估**：它让"配置 → 编译 → 下发 → 快照 → 落库 → 广播"整条链能在 Windows 上完整跑通，这是旧版（必须连
> Debian + 真 PLC）完全做不到的。主文档 10.7 的分层测试策略依赖它。

### 9.4 联调顺序建议

```
1. Windows：stub 跑通全链路（配置→plan→快照→落库→WS）
2. 欧拉：真扩展 + 无设备（验证 list_protocols / health / 空 plan）
3. 欧拉：真扩展 + Modbus 模拟器（ModbusPal / diagslave）验证读
4. 欧拉：真扩展 + 真实设备（逐协议）
5. 欧拉：压力测试（5000 点位 × 1s，看 CPU/内存/抖动）
```

> **第 3 步的 Modbus 模拟器不可省**：直连真 PLC 时"读不到"永远要先怀疑"是代码问题还是 PLC 配置问题"。模拟器能把这两个因素分开。

---

## 十、与旧文档的对照索引

| 旧文档                    | 章节                             | 本文档对应         | 关系                                     |
|---------------------------|----------------------------------|--------------------|------------------------------------------|
| `04-装备通讯协议完善指南` | 一、协议集成现状                 | 1.1 / 1.2          | 结论保留，WS 驱动判为重写                |
| 同上                      | 二、文件清单                     | 1.2 / 4.1          | 迁移映射                                 |
| 同上                      | 第 5 步 连接参数契约             | 4.3                | 一致（新增 `batch_gap`）                 |
| 同上                      | 5.1 参数校验                     | 4.1 强化①          | 从"部分校验"升级为"全量校验 + 定位字段"  |
| 同上                      | 5.2 批量读 + `listAvailable`     | 4.1 强化② / 第六章 | `capability()` 新增                      |
| 同上                      | 5.3 表字段 → `DriverPoint`       | 4.2                | `collection_cycle`(秒) → `cycle_ms`      |
| 同上                      | 第 6 步 地址语义 / MQTT 时间参数 | 4.4                | 完全保留                                 |
| 同上                      | `scale`/`offset` 边界            | 4.5                | 保留 + 双重拦截 `scale=0`                |
| 同上                      | 第 7 步 采集主循环               | 第三章             | **重写**（每设备线程 → 时间轮 + 线程池） |
| 同上                      | 7.1 采集源哪张表                 | 2.3                | **改为 plan 唯一入口**                   |
| 同上                      | 7.2 触发器隐形依赖               | 2.3                | **隐患消除**（不再有触发器）             |
| 同上                      | 7.3 并发安全三项隐患             | 3.4 / 3.5          | 保留互斥，写路径改异步队列               |
| 同上                      | 7.4 两阶段模型                   | 3.2                | **保留为红线**                           |
| 同上                      | 第 8 步 字典补充                 | 06 号文档 3.5      | 字典与扩展清单的双向校验                 |
| 同上                      | 4.2 三套库实测                   | 7.1 / 7.2          | 保留（Debian → openEuler 包名映射）      |
| 同上                      | CMake 开关                       | 7.3                | 保留 OFF 默认 + 宏隔离                   |
| 同上                      | 4.1 Debian 安装清单              | 第八章             | **大幅精简**（去 Drogon/libpq）          |
| `02.01-Cpp_Drogon`        | 驱动工厂 / 采集服务              | 1.2 / 第三章       | 逻辑复用、形态重写                       |
| `01-开发架构指南`         | 2.6 驱动层                       | 第四章             | —                                        |

---

## 十一、待确认（采集扩展侧）

| 编号 | 问题                                                                                   | 影响                      |
|------|----------------------------------------------------------------------------------------|---------------------------|
| K1   | `web_socket` 驱动：轻量自研 vs libwebsockets vs 直接不支持？                           | 1.3；影响构建依赖与实现量 |
| K2   | IO 线程池大小默认 `min(32, cores*2)` 是否合适？现场设备数与点位规模需确认              | 3.2 性能                  |
| K3   | 控制队列满时丢弃策略（拒绝 vs 丢最旧）？控制命令不可丢，故当前选"拒绝"                 | 3.5 / 5.4                 |
| K4   | 按 5min/1h 粒度聚合时，时间边界对齐策略是否需显式指定？                                | 5.5                       |
| K5   | 单实例设备上限 2000 是否够？超过需分实例（多进程各自加载扩展）                         | 5.4                       |
| K6   | openEuler 源里 `open62541-devel` 的版本是否为 1.4+？（若为 1.0/1.2，驱动需按低版本写） | 7.1                       |
| K7   | 是否需要 `debug_hex` 报文级日志开关（现场排障利器，但日志量巨大）                      | 9.3                       |
| K8   | 采样快照缓冲的容量与溢出策略（环形覆盖最旧 vs 拒绝新写入）？影响断库时的数据保留时长   | 05 号 6.3                 |