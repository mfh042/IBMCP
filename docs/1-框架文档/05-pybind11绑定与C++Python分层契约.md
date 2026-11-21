# IBMCP 工业协议采集扩展 · C++ 与 Python 分层契约（pybind11 版）

>
版本：v1.0　　编写日期：2026-11-18　　配套：[01-架构设计](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/01-IBMCP-FastAPI重构架构设计.md) · [02-数据模型](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/02-数据模型与字段契约（FastAPI版）.md) · [03-接口契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/03-接口契约与功能对照（FastAPI版）.md) · [04-驱动契约](file:///E:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP/docs/1-框架文档/04-采集扩展驱动与协议契约（FastAPI版）.md)
>
> **本文档定位**：把"**C++ 协议层**"与"**Python 应用层**"的**关系**定义清楚 —— 谁拥有什么、谁调用谁、数据怎么流、错误怎么传、线程谁管、生命周期谁负责。
> 你给出的示例代码是**一个好的起点骨架**，但其中 12 处设计会导致**生产环境必然出问题**（数据竞争、GIL
> 死锁、无法批量下发、无法多设备、异常穿透、构建不可复现等）。本文档逐条指出问题并给出可直接落地的修正版，同时补齐
> `build.sh` 两段式构建（CMake 编 `.so` + setuptools 打包）链路。
>
> **与 04 号文档的关系**：04 号描述 **驱动侧契约**（协议能力、连接参数、地址语义、线程所有权、快照模型）；本文档描述
> **绑定侧契约**（`Collector` 类的方法签名、GIL 与异常边界、stub 实现、构建安装）。两者**互补不重叠**，共同构成
> `industrial_collector._core` 的完整定义；接口形态一律以本文档为准。

---

## 一、先定一件事：pybind11 扩展还是 ctypes + C ABI

你这次明确要 pybind11。这是**合理的选择**，但必须清楚它与 C ABI 方案的实际差异，否则落地到一半会想改。

| 维度                 | pybind11（本文档，**已选定**）                                           | ctypes + C ABI（**已废弃**）                                                               |
|----------------------|--------------------------------------------------------------------------|--------------------------------------------------------------------------------------------|
| 绑定开发量           | **低**（`PYBIND11_MODULE` 里几十行）                                     | 高（手写 `extern "C"` 导出 + ctypes `argtypes/restype` + 回调 `CFUNCTYPE` + 手工内存管理） |
| 类型安全             | **强**（编译期 + Python 侧类型清晰，IDE 有提示）                         | 弱（全是 `c_void_p` / 字符串 JSON，靠约定）                                                |
| 传复杂结构           | **直接传 STL 容器 / 类**，pybind11 自动转换                              | 必须序列化成 JSON 字符串                                                                   |
| 回调（C++ → Python） | **`std::function`**，pybind11 自动管理 GIL                               | 裸函数指针 + ctypes 强引用坑（主文档 7.3 明确的大坑）                                      |
| 异常传播             | **自动转换**（C++ 异常 → Python 异常，带回溯）                           | 必须手工 `catch(...)`，否则进程崩                                                          |
| GIL 释放             | `py::gil_scoped_release`（本文档用 RAII 封装强制成对，**接口层不可漏**） | ctypes 调用**自动**释放 GIL                                                                |
| 构建产物             | `.so`（Python 扩展模块，**版本绑定**：3.11 编的不能给 3.12 用）          | `.so`（纯 C 共享库，**跨 Python 版本通用**）                                               |
| 热替换               | ⚠ 极难（模块已 loaded）                                                 | ⚠ 也难                                                                                    |
| 调试                 | **好**（异常有 Python 栈 + C++ 栈）                                      | 差（段错误无栈）                                                                           |
| 部署复杂度           | `bash build.sh` 两段式（CMake 编 `.so` + `pip install` 打包，见第七章）    | 需手工放 `.so` + `LD_LIBRARY_PATH`                                                         |
| fork 安全            | 同样有坑（GIL 状态随 fork 复制）                                         | 同样有坑                                                                                   |

**结论（已定，不再讨论）**：采用 **pybind11 扩展形态**。理由：本项目 C++ 侧只做协议采集（4 个驱动 + 调度器），Python 侧承载全部业务与编排，
**性能瓶颈在 IO 等待而非绑定开销**；而 C ABI 方案要在部署上付出"库路径、ABI 匹配、回调强引用、手工 catch"四项持续成本，收益为零。

> **不维护第二套绑定**。04 号文档已按扩展形态改写完毕，其线程模型、驱动契约、plan 结构、快照缓冲设计**全部通用**，只是边界层由
> pybind11 承载。**扩展的线程/调度/零 DB 依赖设计不受绑定形态影响**。

**本文档对 04 号文档的两条红线必须原样继承**：

1. **扩展不写数据库**（Python 独占 DB）；
2. **时间轮 + IO 线程池**（不是每设备一线程）。

---

## 二、分层职责：谁拥有什么

```
┌──────────────────────────────────────────────────────────────┐
│  Python 应用层（FastAPI）                                     │
│  ├─ 配置真相源（running_*）       ← 独占 DB 读写             │
│  ├─ plan_compiler                 ← 编译成 Plan               │
│  ├─ ingest_service                ← 攒批 COPY 入表            │
│  ├─ alarm_engine                  ← 内存态报警判定            │
│  ├─ ws_broadcast                  ← 推前端                    │
│  └─ API / 鉴权 / 审计 / 版本记录                              │
├──────────────────────────────────────────────────────────────┤
│  绑定层 industrial_collector._core（pybind11 生成）          │
│  ├─ 类型转换（DataPoint / Plan / Event ↔ Python 对象）        │
│  ├─ GIL 释放（所有阻塞调用 py::gil_scoped_release）           │
│  └─ 异常转换（C++ → Python）                                  │
├──────────────────────────────────────────────────────────────┤
│  C++ 协议层（纯采集，无 DB、无网络服务、无 JSON 依赖）        │
│  ├─ 驱动：Modbus RTU/TCP、OPC UA、MQTT、Socket、WebSocket     │
│  ├─ 调度：时间轮 + IO 线程池                                  │
│  ├─ 内存：DataStore（点位最新值，加锁读）                     │
│  └─ 事件：采样 / 设备状态 / 写回执                            │
└──────────────────────────────────────────────────────────────┘
```

### 2.1 必须成立的 6 条边界规则

| #  | 规则                                                         | 违反后果                                     |
|----|--------------------------------------------------------------|----------------------------------------------|
| B1 | **DB 只在 Python 访问**，C++ 层零数据库依赖                  | C++ 链上 libpq → 编译/部署双份依赖；配置双源 |
| B2 | **C++ 不感知业务语义**（不知道"报警""等级""审计"）           | 业务规则散落两语言，改一次要编两次           |
| B3 | **C++ 不调用 Python 做阻塞工作**（回调只入队）               | 采集周期被 DB 慢查询拖垮（04 号 5.2）        |
| B4 | **Python 不直接操作驱动对象**（只通过 collector 的公开方法） | 生命周期失控、double-free                    |
| B5 | **阻塞调用必须释放 GIL**                                     | 采集卡住整个 FastAPI（最典型事故）           |
| B6 | **所有 `C++ → Python` 回调都在 C++ 线程里发生**              | Python 侧必须线程安全（见 5.4）              |

### 2.2 与你给出的骨架的对照：哪些保留、哪些必须改

| 你的设计                                               | 判定            | 原因                                                                                                                               |
|--------------------------------------------------------|-----------------|------------------------------------------------------------------------------------------------------------------------------------|
| `DataPoint` 内嵌 `std::mutex` 保护最新值               | ⚠ **改**       | 每点一把 mutex，5000 点 = 5000 个锁对象；且 pybind11 导出的类**含 mutex 时不可拷贝**，易踩坑。改为**集中式 `DataStore`**（见 3.2） |
| `set(double, int)` 内取 `system_clock::now()`          | ⚠ **改**       | 时间戳应在**采样时刻**取（读成功的那一刻），不是"赋值那一刻"；换算后取更合理                                                       |
| `ModbusCollector` 每实例一个 `std::thread`             | ❌ **必须改**   | N 设备 = N 线程；且**线程在 C++ 里直接调 `point.set()`**，与 Python 读取无同步语义。改为线程池（见第四章）                         |
| `ModbusConfig` 单 addr/count                           | ❌ **必须改**   | 一台设备只采 1 个寄存器，无法表达 100 点；且没有 `scale/offset/dataType`，无法解码                                                 |
| `start(DataPoint&)` 传引用                             | ⚠ **改**       | C++ 线程持有 Python 侧对象引用 → 对象被 GC 后野指针。改为 C++ 内部拥有存储                                                         |
| `OpcUaCollector::read_into` 里 `v != 0 ? 1 : 0` 判质量 | ❌ **必须改**   | `v==0` 是合法读数，不是"失败"。质量码必须来自 `UA_StatusCode`                                                                      |
| `MQTTClient`（Paho **C** 库）                          | ⚠ **改**       | 项目已实测依赖 `libmosquitto`（04 号 7.1）。引入 Paho 等于多一套库 + 多一套构建依赖。**除非你确定要换**，否则统一 mosquitto        |
| `MqttPublisher` 只 publish 不 subscribe                | ❌ **必须改**   | 业务是**采集设备数据**（subscribe），publish 是"上报到别的系统"，两者混淆                                                          |
| `publish` 每次阻塞等 ack                               | ⚠ **改**       | 高频下每次等 ack 会串行化；改异步 + 队列                                                                                           |
| `bindings.cpp` 直接导出驱动类                          | ⚠ **改**       | 导出驱动 = B4 违约；只导出 `Collector` + 数据类                                                                                    |
| 无异常处理                                             | ❌ **必须改**   | pybind11 会把 C++ 异常转成 Python 异常，但**在 C++ 线程里抛出的异常无法被 Python 捕获** → `std::terminate`                         |
| `find_package(pybind11)` + 手写 `.so` 拷贝             | ⚠ **改**       | 不可复现；需 `build.sh` 用 `cmake --install --prefix python` 落到固定位置（见第七章）                                               |
| CMake `CXX_STANDARD 17`                                | ✅ 保留（够用） | 04 号文档建议 20 是因为用了 `std::jthread`，用 `std::thread` 则 17 足够                                                            |
| `ModbusCodec` 地址解析                                 | ✅ **必须保留** | 你的骨架缺失，`addr=0` 只能读保持寄存器 0 偏移。真实场景需 `40001/30001/0xxxx/1xxxx` 语义（04 号 4.4）                             |

---

## 三、C++ 侧：数据模型与存储

### 3.1 三层数据对象（清晰区分"配置 / 运行 / 采样"）

```cpp
// ---------- 1. 配置态（来自 Python 的 Plan，只读） ----------
struct PointSpec {
    int64_t     point_id   = 0;      // = running_device_collection.id
    std::string address;             // "40001" / "ns=2;s=Motor.Speed" / "plant/plc/temp"
    std::string data_type  = "float";// bool/int/float/double/string
    double      scale      = 1.0;
    double      offset     = 0.0;
    int         cycle_ms   = 1000;
    double      alarm_high = 0.0;    // 仅透传，C++ 不判定
    double      alarm_low  = 0.0;
    bool        alarm_enabled = false;
    std::string read_write = "R";
    std::string unit;
};

struct DeviceSpec {
    std::string device_key;          // = device_code
    std::string protocol;            // modbus_tcp / opc_ua / mqtt ...
    std::string connection_json;     // 原样透传给驱动
    std::vector<PointSpec> points;
};

// ---------- 2. 采样态（驱动产出，回调给 Python） ----------
struct Sample {
    std::string device_key;
    int64_t     point_id = 0;
    double      value    = 0.0;      // 已换算的工程值
    int         quality  = 0;        // 0=Bad 1=Good 2=Uncertain
    int64_t     ts_ms    = 0;        // 采样时刻（UTC 毫秒）
    std::string error;               // quality=Bad 时说明原因
};

// ---------- 3. 运行态（设备连接状态） ----------
struct DeviceStatus {
    std::string device_key;
    int         status = 0;          // 0=离线 1=在线 2=异常
    std::string reason;              // connect_timeout / auth_failed / ...
    int64_t     ts_ms  = 0;
};
```

> **为什么不用你的 `DataPoint` 单类承载三件事**
> ：配置（不变）、采样（每次都变）、状态（偶发）的生命周期与并发特征完全不同。混一类会导致"每个采样都复制一份配置"或"配置被采样覆盖"。
> **分开是必须的**。

### 3.2 采样存储：集中式 `DataStore`，不是每点一把锁

```cpp
class DataStore {
public:
    // 采集线程调用：覆盖最新值并递增版本
    void upsert(const Sample &s) {
        std::unique_lock lk(mtx_);
        Slot &slot = slots_[key(s.device_key, s.point_id)];
        slot.sample = s;
        slot.seq    = ++seq_;            // 该点最后一次变化的版本号
    }

    // Python 拉取：自 since_seq 之后发生变化的最新值（批量）
    // 返回值第一项 = 本轮游标，下次调用原样传回
    std::pair<uint64_t, std::vector<Sample>> snapshot_since(uint64_t since_seq) const {
        std::shared_lock lk(mtx_);            // mtx_ 必须 mutable（见 private 段）
        const uint64_t cur = seq_;            // seq_ 与 slots_ 同锁内读，保持一致
        std::vector<Sample> out;
        if (since_seq == 0) {                  // 首轮：全量
            out.reserve(slots_.size());
            for (const auto &kv : slots_) out.push_back(kv.second.sample);
        } else {
            for (const auto &kv : slots_)
                if (kv.second.seq > since_seq) out.push_back(kv.second.sample);
        }
        return {cur, std::move(out)};
    }

private:
    struct Slot {
        Sample   sample;
        uint64_t seq = 0;
    };
    static std::string key(const std::string &d, int64_t p) {
        return d + '\x1f' + std::to_string(p);
    }
    mutable std::shared_mutex mtx_;           // ← mutable：const 方法里要加共享锁
    std::unordered_map<std::string, Slot> slots_;
    mutable uint64_t seq_ = 0;                // ← mutable：同上，或改为不读 seq_（见下）
};
```

> **`seq_` 与 `slots_` 必须在同一把锁内读**：`cur` 一旦在锁外取或与 `slots_` 遍历分两次取，pump
> 可能拿到"游标已推进、但对应点还没读出来"的组合，**下一轮就会永久漏掉那批点**。这是"增量拉取"最隐蔽的一类丢数据 bug ——
> 本文档代码把两者放在同一个 `shared_lock` 作用域内，就是为杜绝它。
>
> **`seq_` 的另一个简化**：`cur` 也可以直接取"本轮所有 `slot.seq` 的最大值"（`max_element`），这样 `seq_` 连 `mutable`
> 都不需要、也不存在两个计数器不一致的可能。5000 点在 `shared_lock` 内做一次 max 是纳秒级，**推荐这种写法**。

| 设计                             | 理由                                                                                                                                |
|----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------|
| **一把读写锁**（`shared_mutex`） | 读多写多；`unordered_map` + 单锁在 5000 点 × 1s 下完全够（每次 upsert 纳秒级）                                                      |
| **按 key 覆盖**（不做队列）      | 业务只要"最新值"；队列会让积压无限增长                                                                                              |
| **每点带 `seq`**（不是全局重建） | "只取变化"必须按点比较版本；若只保留全局 `seq_`，199ms 内未变化的点会被每轮重复返回，**200ms 拉取等于每轮全量**，拉取的意义荡然无存 |
| **`since_seq=0` 返回全量**       | 启动首轮与 pump 重启的确定性语义，不用额外"是否首轮"标志                                                                            |
| **返回 `(cursor, samples)`**     | 游标由 Python 持有并原样回传，C++ 侧不做有状态游标（状态在边界外，便于 pump 重启/多泵并存）                                         |

> **你的 `DataPoint` 每实例带 `std::mutex` 的问题**：pybind11 默认要求类型可拷贝（`py::init<>` + 传参时可能触发拷贝构造），而
> `std::mutex` 不可拷贝，会在编译期报一串晦涩错误，或被迫加 `py::nodelete` 等 hack。**集中存储天然规避**。

### 3.3 两种"给 Python 拿数据"的方式，必须选"拉"

| 方式           | 说明                                 | 判定                                    |
|----------------|--------------------------------------|-----------------------------------------|
| **推（回调）** | C++ 每次采样调 Python 回调           | ✅ 用于**事件通知**（设备状态、写回执） |
| **拉（快照）** | Python 定时调 `snapshot_since`       | ✅ 用于**高频采样数据**（批量、可控）   |
| 直接读对象     | Python 持有 `DataPoint` 反复 `get()` | ❌ 每点一次跨语言调用 + 一把锁          |

**推荐组合（本文档采用）**：

```
采样数据：C++ 写入 DataStore（无回调） → Python 每 200ms 调 snapshot_since() 拉取
设备状态：C++ 通过回调推（低频，必须即时）
写回执：  C++ 通过回调推（人工操作，必须即时）
```

> **为什么采样不用回调推**：5000 点 × 1s = 5000 次/秒的 C++→Python 跨语言调用，每次都要 `GIL 获取 + 对象构造 + 引用计数`
> ，实测开销远高于"200ms 一次批量拉 1000 条"。**拉的模式把跨语言调用次数从 5000/s 降到 5/s**。

---

## 四、线程模型（必须与 04 号文档一致）

### 4.1 结构

```
┌────────────────────────────────────────────────────┐
│ Timer Thread（1 个）：时间轮 tick=50ms，只做调度    │
└─────────────────────┬──────────────────────────────┘
                      │ push 到期设备任务
                      ▼
┌────────────────────────────────────────────────────┐
│ TaskQueue（有界 MPMC）                              │
└─────────────────────┬──────────────────────────────┘
                      │ pop
                      ▼
┌────────────────────────────────────────────────────┐
│ IO Pool（N = min(32, cores*2)）                     │
│ worker：ioMutex.lock → driver.readPoints            │
│         → toEngineValue → DataStore.upsert          │
│         → （仅状态变化时）回调 Python               │
└────────────────────────────────────────────────────┘
```

### 4.2 你骨架里"每设备一个 `std::thread`"的三个必然后果

| # | 问题            | 表现                                |
|---|-----------------|-------------------------------------|
| 1 | 线程数 = 设备数 | 500 设备 = 500 线程，上下文切换吃掉 |

| 2 | 线程直接改共享点值 | Python 读到的是"任意时刻"的值，无批量语义，无法保证同一轮的一致性 |
| 3 | `start(DataPoint&)` 持 Python 对象引用 | 若 Python 侧 `del point` 或 GC → C++ 线程写野指针 → **必崩** |

### 4.3 关键实现约束

```cpp
// 正确：worker 线程里绝不持有 Python 对象
void Collector::worker_loop() {
    while (running_) {
        auto task = queue_.pop();                      // DeviceSpec 的 shared_ptr
        if (!task) continue;

        auto dev = get_device(task->device_key);       // 内部 map，自有所有权
        if (!dev) continue;

        std::lock_guard io_lk(dev->io_mutex);
        std::vector<Sample> batch;
        dev->driver->readPoints(dev->point_snapshot, batch);   // 纯 C++，无 Python

        for (auto &s : batch) store_.upsert(s);        // ← 不需要 GIL

        if (online_state_changed(dev))
            emit_status(dev);                          // 回调 Python（内部 acquire GIL）
    }
}
```

> **`DataStore::upsert` 绝对不能包 GIL**。这是最容易画蛇添足的地方：C++ 采集线程若在每次 `upsert` 前
> `py::gil_scoped_acquire`，就会与 FastAPI 主线程**抢同一把 GIL**，5000 点 × 1s 下变成"扩展把 GIL 占满、请求排队"——
> **加了锁反而更慢**。**只有真正触碰 Python 对象（构造 `py::dict`、调用回调）时才需要 GIL**。

### 4.4 GIL 规则表（pybind11 方案的核心纪律）

| 位置                          | 是否需要 GIL | 写法                                                                   |
|-------------------------------|--------------|------------------------------------------------------------------------|
| Python 调 `collector.start()` | 已持有       | 内部阻塞部分必须 `py::gil_scoped_release`                              |
| Python 调 `snapshot_since()`  | 已持有       | 转换 `std::vector<Sample>` → `list` 时需要；**C++ 取数据段可临时释放** |
| Python 调 `apply_plan()`      | 已持有       | 解析 + 校验 + 建驱动可释放；**回调注册处需持有**                       |
| C++ worker 线程内             | **未持有**   | 调 Python 回调前 `py::gil_scoped_acquire`                              |
| C++ 析构（Python GC 触发）    | 已持有       | `stop()` join 线程时**必须释放**，否则死锁（见 4.5）                   |

### 4.5 最致命的一个坑：`stop()` 死锁

```cpp
// ❌ 错误：Python 调 stop() 时持有 GIL，join 等待的 worker 线程
//    若正在等 GIL（准备回调）→ 互相等待 → 死锁
void Collector::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();   // 死锁点
}
```

```cpp
// ✅ 正确：join 前释放 GIL
void Collector::stop() {
    {
        py::gil_scoped_release release;      // 关键
        running_ = false;
        cv_.notify_all();
        for (auto &t : workers_) if (t.joinable()) t.join();
    }
    // 此处 GIL 已重新获取
}
```

**这条必须在 `stop()`、`~Collector()`、`apply_plan()`（移除设备时）、`shutdown()` 全部遵守**。04 号文档 5.3 的"崩溃边界"里
`dlclose` 前必须 join，同理。

> 排查特征：**程序启动正常、采集正常、一按"停止"或退出就卡死不退**，且 `Ctrl+C` 也无效（信号处理也拿不到 GIL）。出现此现象 100%
> 是这条。

### 4.6 回调注册与强引用

```cpp
using EventCb = std::function<void(std::string /*json*/)>;

void Collector::set_event_callback(EventCb cb) {
    std::lock_guard lk(cb_mtx_);
    cb_ = std::move(cb);            // std::function 内部持有 Python 对象引用 → 强引用
}

void Collector::emit_status(const DeviceSpec *dev) {
    EventCb cb;
    {
        std::lock_guard lk(cb_mtx_);
        cb = cb_;                    // 拷贝出来，避免持锁调外部代码
    }
    if (!cb) return;
    // 当前线程若来自 worker（无 GIL），std::function 调 Python 会自动加 GIL
    cb(make_status_json(dev));
}
```

| 要点                                                     | 说明                                                                                                   |
|----------------------------------------------------------|--------------------------------------------------------------------------------------------------------|
| **`std::function` 自动强引用**                           | 这正是 pybind11 比 ctypes 好的地方：不用手写"保持回调不回收"的 hack（主文档 7.3 的坑在这里天然不存在） |
| **不要在持内部锁时调回调**                               | 先拷贝 `cb` 再调，防回调里反向调 collector 造成死锁                                                    |
| **回调必须在 `stop()` 前 `set_event_callback(nullptr)`** | 否则 Python 解释器关闭阶段可能收到回调 → 崩溃                                                          |
| **回调里禁止长耗时**                                     | 同 04 号 5.2：只入队                                                                                   |

---

## 五、Python 侧：包装层设计

### 5.1 目录结构（可直接落地）

```
backend/
├── app/
│   ├── core/
│   │   └── collector/             # ← 扩展接入适配层（01/04 号旧称 app/core/kernel，已统一为本目录）
│   │       ├── __init__.py        # 对外门面：get_collector()
│   │       ├── protocol.py        # CollectorApi Protocol + SampleOut/StatusOut（唯一接口真相源）
│   │       ├── ffi.py             # 绑定真实 _core（生产）
│   │       ├── stub.py            # 桩实现（Windows / CI）
│   │       ├── dispatch.py        # EventDispatcher：C++ 线程事件 → asyncio
│   │       └── pump.py            # SamplePump：200ms 拉取
│   ├── services/
│   │   ├── plan_compiler.py       # 编译 running_* → Plan（复刻同一套校验）
│   │   └── ingest_service.py      # 批量 COPY 入表 + 报警判定
│   └── main.py                    # lifespan 里 init/shutdown
├── collector-cpp/                 # ← C++ 扩展工程（04 号 8.1 目录结构）
└── pyproject.toml
```

> **目录名统一为 `app/core/collector/`**。01 号 4.x 与 04 号 7.3 曾写 `app/core/kernel/`，那是"C ABI
> 时代"的遗留命名；绑定形态定为扩展后统一改本目录，**全文档不再出现 `app/core/kernel/`**。

### 5.2 顶层接口（唯一对外契约）

```python
# app/core/collector/protocol.py
from __future__ import annotations
from typing import Protocol, Callable
from dataclasses import dataclass

@dataclass(slots=True, frozen=True)
class SampleOut:
    device_key: str
    point_id: int
    value: float
    quality: int         # 0=Bad 1=Good 2=Uncertain
    ts_ms: int
    error: str = ""

@dataclass(slots=True, frozen=True)
class DeviceStatusOut:
    device_key: str
    status: int          # 0=离线 1=在线 2=异常
    reason: str = ""
    ts_ms: int = 0

class CollectorApi(Protocol):
    def init(self, cfg: dict) -> None: ...
    def apply_plan(self, plan: dict, revision: int) -> int: ...   # 0=成功；校验失败抛 CollectorError 子类
    def set_event_callback(self, cb: Callable[[dict], None] | None) -> None: ...
    def snapshot_since(self, since_seq: int) -> tuple[int, list[SampleOut]]: ...
    def write_point(self, device_key: str, point_id: int, value: str) -> int: ...  # 返回 command_id
    def list_protocols(self) -> list[str]: ...
    def protocol_capabilities(self) -> dict[str, dict]: ...
    def health(self) -> dict: ...
    def shutdown(self) -> None: ...
```

| 签名决策                                          | 口径                              | 依据                                                                                                                                         |
|---------------------------------------------------|-----------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------|
| `snapshot_since(since_seq) -> (new_seq, samples)` | **Python 传游标、C++ 回游标**     | 03 号与 04 号都要求"只取变化"；无游标的 `snapshot_since()` 会让每轮都全量返回 5000 点。委托方持有 `since_seq`（pump 内部成员），不跨线程共享 |
| `list_protocols() -> list[str]`                   | **只返回规范协议名清单**          | 04 号 6.x 已定；能力查询拆到 `protocol_capabilities()`（04 号标注"可选扩展接口"），两个接口职责不重叠                                        |
| `protocol_capabilities()`                         | 单独方法，不塞进 `list_protocols` | 避免 `list_protocols` 返回异构结构，前端下拉与后端置灰是两个用途                                                                             |

> **`snapshot_since` 的游标语义**：`since_seq=0` 表示"取当前全量快照"（启动首轮）；此后每次用上一轮返回的 `new_seq`。C++ 侧以
> `new_seq` 为"本轮快照的时间点"，**不会漏也不会重复**（见 3.2 的 `seq_` 快照生成规则）。

> **`Protocol` 的好处**：`ffi.py` 与 `stub.py` 都实现它，业务代码**无需 `if platform` 分支**，测试与生产走同一路径。

### 5.3 真实绑定层（薄，只做转换）

```python
# app/core/collector/ffi.py
from __future__ import annotations
import logging, threading
from typing import Callable
from .protocol import SampleOut, CollectorApi

log = logging.getLogger(__name__)

class PybindCollector:
    """薄封装：只做 pybind 对象 ↔ 业务对象的转换。不做业务逻辑。"""

    def __init__(self) -> None:
        from industrial_collector import _core          # 延迟导入，便于 stub 环境
        self._core = _core
        self._cb: Callable[[dict], None] | None = None

    def init(self, cfg: dict) -> None:
        self._core.init(cfg)

    def apply_plan(self, plan: dict, revision: int) -> int:
        return int(self._core.apply_plan(plan, revision))

    def set_event_callback(self, cb: Callable[[dict], None] | None) -> None:
        # 必须自己持有一份强引用（虽然 std::function 也持有，
        # 但 Python 侧若不持有，解释器关闭阶段的顺序可能出问题）
        self._cb = cb
        self._core.set_event_callback(cb)

    def snapshot_since(self, since_seq: int) -> tuple[int, list[SampleOut]]:
        # C++ 返回 (new_seq, list[tuple])，此处按 SampleOut 字段顺序组装
        new_seq, rows = self._core.snapshot_since(since_seq)
        return new_seq, [SampleOut(*r) for r in rows]

    def write_point(self, device_key: str, point_id: int, value: str) -> int:
        return int(self._core.write_point(device_key, point_id, value))

    def list_protocols(self) -> list[str]:
        return list(self._core.list_protocols())

    def protocol_capabilities(self) -> dict[str, dict]:
        return dict(self._core.protocol_capabilities())

    def health(self) -> dict:
        return self._core.health()

    def shutdown(self) -> None:
        self.set_event_callback(None)      # 先摘回调，再停
        self._core.shutdown()
```

### 5.4 事件回调必须线程安全（来自 04 号 5.2）

```python
# app/core/collector/pump.py 与回调解耦
class EventDispatcher:
    """把 C++ 线程的事件搬到 asyncio 世界，回调本身只入队。"""

    def __init__(self, loop: asyncio.AbstractEventLoop, maxsize: int = 20000):
        self._loop = loop
        self._q: queue.Queue[dict] = queue.Queue(maxsize=maxsize)
        self._dropped = 0

    def on_event(self, payload: dict) -> None:
        """在 C++ 线程中调用 —— 绝不阻塞、绝不 await、绝不加锁。"""
        try:
            self._q.put_nowait(payload)
        except queue.Full:
            self._dropped += 1              # 原子计数，无锁

    async def run(self) -> None:
        while True:
            payload = await self._loop.run_in_executor(None, self._q.get)
            await self._handle(payload)     # 真正的 DB/WS 处理在这里
```

| 纪律                                        | 原因                                                          |
|---------------------------------------------|---------------------------------------------------------------|
| 回调里**只 `put_nowait`**                   | 04 号 5.2：回调 > 50ms 会反压扩展线程                         |
| 队列满**丢弃最旧/计数**                     | 绝不阻塞回调，绝不丢 `device_status` / `write_result`（见下） |
| **`device_status` / `write_result` 不可丢** | 丢状态 → 前端永远显示离线；丢回执 → 控制永远无结果            |
| 采样数据**不走这里**                        | 走 `snapshot_since` 拉取（3.3）                               |

> **队列分级**：建议两个队列 —— `ctrl_q`（状态/回执，容量 2000，满则等待 5ms 后强制入）与 `stat_q`（低频统计）。采样
> **不占队列**。

### 5.5 采样拉取泵

```python
# app/core/collector/pump.py
class SamplePump:
    def __init__(self, collector: CollectorApi, ingest, interval_ms: int = 200):
        self._c = collector
        self._ingest = ingest
        self._interval = interval_ms / 1000.0
        self._since_seq = 0          # ← 游标由 pump 独占持有，不跨线程
        self._task: asyncio.Task | None = None

    async def run(self) -> None:
        while True:
            await asyncio.sleep(self._interval)
            try:
                # 到线程池执行：C++ 侧要拿 shared_lock + 拷贝，不能占事件循环
                new_seq, samples = await asyncio.to_thread(
                    self._c.snapshot_since, self._since_seq
                )
            except CollectorError as e:
                log.warning("snapshot_since failed, keep cursor: %s", e)
                continue                     # 游标不推进，下一轮重试
            self._since_seq = new_seq        # 仅成功后推进
            if samples:
                await self._ingest(samples)
```

| 决策                   | 值                               | 理由                                                                 |
|------------------------|----------------------------------|----------------------------------------------------------------------|
| 拉取周期               | **200ms**                        | 与主文档"事件 → 落库 p99 < 300ms"对齐；5000 点 × 1s 时每轮约 1000 条 |
| 用 `asyncio.to_thread` | ✅                               | `snapshot_since` 内的 C++ 转换可能几十毫秒，不能占事件循环           |
| 采样不做去重           | 按 `(device_key, point_id)` 覆盖 | 只关心最新值                                                         |
| 空轮跳过               | `if samples`                     | 避免空 COPY                                                          |

> **`to_thread` 使用注意**：默认线程池上限 32；pump 只有 1~2 个并发，无压力。但**不要**在这里 `await` 耗时长任务（COPY
> 入表应交给独立 ingest worker，见 5.6）。

### 5.6 采样 → 落库 → 广播的完整链路

```
C++ worker ──upsert──► DataStore
                            │
Python SamplePump（200ms）──┴──► snapshot_since() ──► IngestService
                                                          │
                                    ┌─────────────────────┼─────────────────────┐
                                    ▼                     ▼                     ▼
                            批量 COPY 入表          报警判定（内存态）        WS 广播
                            （COPY，非 INSERT）      阈值来自 Plan           （data_batch）
```

> 与 04 号文档 5.2 一致：**广播不等待落库**，落库失败只记日志。

### 5.7 生命周期：放在 `lifespan`，不在模块导入时

```python
# app/main.py
@asynccontextmanager
async def lifespan(app: FastAPI):
    cfg = settings.collector_cfg()
    collector = get_collector()          # ffi 或 stub，按 settings 选择
    collector.init(cfg)                  # ← 必须在 fork 之后（uvicorn 不用 --preload）

    dispatcher = EventDispatcher(asyncio.get_running_loop())
    collector.set_event_callback(dispatcher.on_event)

    app.state.collector = collector
    app.state.dispatcher = dispatcher

    tasks = [
        asyncio.create_task(dispatcher.run()),
        asyncio.create_task(SamplePump(collector, ingest).run()),
    ]

    replay_plan_on_startup(collector)    # 崩溃恢复（04 号 5.3 / 主文档 7.7）

    try:
        yield
    finally:
        for t in tasks:
            t.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        collector.set_event_callback(None)   # 先摘
        await asyncio.to_thread(collector.shutdown)   # ← 释放 GIL / 不占事件循环
```

| 约束                         | 原因                                       |
|------------------------------|--------------------------------------------|
| **禁止 `uvicorn --preload`** | fork 后线程全丢、GIL 状态异常（04 号 5.3） |
| `init` 必须在 `lifespan`     | 同上                                       |
| `shutdown` 用 `to_thread`    | `stop()` 会 join 线程，可能耗时数百 ms     |
| 先摘回调再 shutdown          | 防止关闭阶段的回调进入已关闭的解释器       |

### 5.8 Windows / CI 用的 stub

```python
# app/core/collector/stub.py
class StubCollector(CollectorApi):
    """与真实现同接口；用确定性公式产生采样，可模拟慢/崩/失败。"""
    def __init__(self, simulate: dict | None = None): ...
    def list_protocols(self) -> list[dict]:
        return [{"protocol": "modbus_tcp", "supports_read": True, "supports_write": True},
                {"protocol": "opc_ua",     "supports_read": True, "supports_write": True},
                {"protocol": "mqtt",       "supports_read": True, "supports_write": False}]
    # apply_plan 必须复刻 4001/4002 等校验规则（04 号 9.3）
```

工厂：

```python
def get_collector() -> CollectorApi:
    if settings.collector_backend == "stub":
        from .stub import StubCollector
        return StubCollector()
    from .ffi import PybindCollector
    return PybindCollector()
```

---

## 六、Plan 与事件的跨语言格式（减少转换开销）

### 6.1 不要用 JSON 字符串过边界

| 方案                                                                | 5000 点 plan 耗时（量级）          | 判定 |
|---------------------------------------------------------------------|------------------------------------|------|
| `apply_plan(plan_json_str)`                                         | 序列化 + 反序列化 2 次，约 20~40ms | ❌   |
| `apply_plan(dict)`（pybind11 自动转 `nlohmann::json` / 自定义结构） | 直接构造 C++ 对象，约 2~5ms        | ✅   |

**推荐**：pybind11 传 **`dict` / `list`**，在 `bindings.cpp` 里转成 C++ 结构；**不要**在 Python 侧 `json.dumps` 再让 C++
解析。

```cpp
// bindings.cpp 里做最小结构转换
static int apply_plan(Collector &c, const py::dict &plan, int64_t revision) {
    std::vector<DeviceSpec> specs;
    for (auto item : plan["devices"].cast<py::list>()) {
        auto d = item.cast<py::dict>();
        DeviceSpec ds;
        ds.device_key    = d["device_key"].cast<std::string>();
        ds.protocol      = d["protocol"].cast<std::string>();
        ds.connection_json = py::str(d["connection"]);   // 仅此处序列化，量小
        for (auto p : d["points"].cast<py::list>()) { ... }
        specs.push_back(std::move(ds));
    }
    return c.apply_plan(std::move(specs), revision);
}
```

> `connection` 因为是**异构结构**（按协议不同），保留字符串形式最小化改动；点位是**同构结构**，直接转 struct 才有意义。

### 6.2 `snapshot_since` 的返回格式：`list[tuple]` 最快，`list[dict]` 最可读

```cpp
// 最快：返回 (new_seq, list[tuple])，Python 侧用 dataclass 组装
py::tuple snapshot_since(uint64_t since_seq) {
    auto [new_seq, rows] = store_.snapshot_since(since_seq);
    py::list out(rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
        out[i] = py::make_tuple(rows[i].device_key, rows[i].point_id, rows[i].value,
                                rows[i].quality, rows[i].ts_ms, rows[i].error);
    return py::make_tuple(new_seq, out);
}
```

> 每轮 1000 条时，`list[dict]` 约比 `list[tuple]` 慢 2~3 倍。**若 ingest 已经用 `copy_records_to_table` 吃
> tuple，这个格式可直接对接，无需再转**。
>
> **字段顺序即契约**：`SampleOut(*r)` 的位置解包依赖这个顺序，**改动 C++ `make_tuple` 的字段顺序等于破坏 Python 侧**
> 。若必须增删字段，只允许追加到末尾并同步改 `SampleOut`，且需在 06 号报告的兼容性说明中登记。

### 6.3 事件 payload（低频，用 dict 即可）

```json
{ "type": "device_status", "device_key": "DEV-001", "status": 0, "reason": "connect_timeout", "ts_ms": 1729713600000 }
```

```json
{ "type": "write_result", "command_id": 42, "device_key": "DEV-001", "point_id": 101,
  "success": true, "status": 2, "message": "ok", "ts_ms": 1729713601000 }
```

```json
{ "type": "plan_ack", "revision": 1024, "status": 1, "device_count": 12, "point_count": 480,
  "code": 0, "message": "ok", "ts_ms": 1729713602000 }
```

> **扩展侧命名为最终真相源**：`device_key`（不是 `device_id`，不是 `device_code`）、`ts_ms`（毫秒，不是 `ts`）。03 号 5.2 的 WS
> 载荷用 `device_code` + `ts`（秒）是**面向浏览器的投影**，投影只在 Python 的 `EventDispatcher` 一处发生（见 03 号 5.2①）。
> **扩展自身边界内绝不出现驼峰键，也绝不出现 `device_code`/`ts` 这两个投影名。**
>
> 01 号 7.3 的事件表曾写 `ts`，那是与 WS 投影混写；**事件契约一律以本节为准**。

---

## 七、构建与安装（项目根 `pyproject.toml` + `build.sh`）

### 7.1 实际构建方案：`setuptools` + CMake 两段式

> ⚠️ **本章已按实测代码校正（2026-11-20）**。此前版本假设用 `scikit-build-core` 一次 `pip install` 完成"编译 + 打包"，
> **与仓库实际实现不符**：真实方案是 **`build.sh` 先 CMake 编译并把 `.so` 装进源码目录，再 `pip install .`（setuptools）打包**。
> 两者都能得到可用结果，但**不要混用口径**——按本方案操作才能复现仓库现状。

**真实两段式流程（`deployments/openeuler/build.sh`）**

```bash
# 第 1 段：CMake + pybind11 编译，install 到源码目录
cmake -S collector-cpp -B collector-cpp/build \
    -DCMAKE_BUILD_TYPE=Release \
    -Dpybind11_DIR="$(python -c 'import pybind11; print(pybind11.get_cmake_dir())')" \
    -DPython_EXECUTABLE="$VIRTUAL_ENV/bin/python"
cmake --build collector-cpp/build -j"$(nproc)"
cmake --install collector-cpp/build --prefix python     # → python/industrial_collector/_core.so

# 第 2 段：setuptools 打包安装（package-data 把 .so 一起带上）
pip install .
```

**`pyproject.toml`（项目根，实测内容）**

```toml
[build-system]
requires = ["setuptools>=68", "wheel"]
build-backend = "setuptools.build_meta"

[project]
name = "ibmcp"                 # 注意：不是 industrial-collector
version = "0.1.0"
requires-python = ">=3.11"

[tool.setuptools.packages.find]
where = ["backend", "collector-python"]  # backend/app 与 python/industrial_collector 一起被收进来

[tool.setuptools.package-data]
industrial_collector = ["*.so", "*.pyd", "py.typed"]
```

> **`package-data` 是"pip 装完 import 报错"的第一号原因**。没有它，`pip install .` 只带走 `__init__.py` 等 `.py` 文件，
> `_core.so` 留在构建产物目录里不进 venv，`import industrial_collector._core` 必然 `ModuleNotFoundError`。
>
> **`--prefix python` 与 `install(TARGETS _core LIBRARY DESTINATION industrial_collector)` 必须配对**：CMakeLists.txt 里的
> `DESTINATION industrial_collector` 是相对路径，靠 `--prefix python` 才落到 `python/industrial_collector/_core.so`。
> 若改成 `--prefix .` 会装到项目根的 `industrial_collector/`，`packages.find` 找不到它。
>
> **为什么不是 scikit-build-core**：`scikit-build-core` 也能做到同样效果，但需要把 `cmake.source-dir`、`wheel.packages`
> 在 `pyproject.toml` 里再写一套，与 `build.sh` 的 CMake 参数形成两份真源。当前选择"CMake 显式两段 + setuptools 打包"，
> 好处是 `build.sh` 里的编译参数（`pybind11_DIR`/`Python_EXECUTABLE`）可读可调，坏处是必须按顺序跑两段。

```cmake
# collector-cpp/CMakeLists.txt 关键片段
cmake_minimum_required(VERSION 3.20)
project(ibmcp_collector LANGUAGES CXX)     # 实测工程名是 ibmcp_collector
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(pybind11 CONFIG REQUIRED)

# 协议库用 pkg-config 探测，找到才编（缺库时降级，而不是构建失败）
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MODBUS QUIET libmodbus)
    pkg_check_modules(MOSQUITTO QUIET libmosquitto)
    pkg_check_modules(OPEN62541 QUIET open62541)
endif()

# 目录名与文件名遵循 snake_case（实测 src/drivers/driver_factory.cpp）
set(COLLECTOR_SOURCES
    src/bindings/module.cpp          # 唯一 pybind11 导出点
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

# 协议库探测成功才启用（缺库降级为 OFF，不中断构建 —— 与实测 CMakeLists.txt 一致）
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

# 相对路径 DESTINATION，靠 build.sh 的 `--prefix python` 落到 python/industrial_collector/
install(TARGETS _core LIBRARY DESTINATION industrial_collector)
```

```bash
# 完整构建（openEuler 侧）—— 两段式，等价于 build.sh 的内容
bash deployments/openeuler/build.sh

# 手工等价操作（排查时用）：先编译装 .so，再打包
cmake --build collector-cpp/build -j"$(nproc)"
cmake --install collector-cpp/build --prefix python     # → python/industrial_collector/_core.so
pip install .                                          # setuptools 打包，package-data 带上 .so

# 产出 wheel 分发（需先完成上面 CMake install 段，否则 wheel 里没有 .so）
pip wheel . -w dist --no-deps
```

> ⚠️ **不要运行 `pip install ./backend`**：`backend/` 下没有 `pyproject.toml`，`pyproject.toml` 在项目根。
> 且在 setuptools 方案下 `pip install` **不会**触发 CMake 编译，`.so` 必须先由 `build.sh` 产出——这是与
> `scikit-build-core` 方案最关键的行为差异。

### 7.2 包目录布局（`industrial_collector` 必须在 site-packages 里）

```
site-packages/
└── industrial_collector/
    ├── __init__.py          # from ._core import *
    └── _core.cpython-311-x86_64-linux-gnu.so
```

```python
# python/industrial_collector/__init__.py
from ._core import (
    Collector, init, apply_plan, set_event_callback, snapshot_since,
    write_point, list_protocols, protocol_capabilities, health, shutdown,
    CollectorError, ProtocolNotSupported, InvalidConnection,
    PlanRevisionConflict, NotInitialized, ControlQueueFull, DeviceNotInPlan,
)
__all__ = [
    "Collector", "init", "apply_plan", "set_event_callback", "snapshot_since",
    "write_point", "list_protocols", "protocol_capabilities", "health", "shutdown",
    "CollectorError", "ProtocolNotSupported", "InvalidConnection",
    "PlanRevisionConflict", "NotInitialized", "ControlQueueFull", "DeviceNotInPlan",
]
```

> **异常类必须一起导出**：只导出方法而不导出异常类，Python 侧只能 `except Exception` 兜底，`to_api_error()` 拿不到 `rc`/
> `stage`，4001~4006 全部退化成 500。**这条写进验收清单**。

> `[tool.setuptools.package-data]` 里必须含 `industrial_collector = ["*.so", ...]`，已在 7.1 写入配置。**这是"pip 装完
> import 报错"的第一号原因**：`.so` 由 `install(TARGETS _core ...)` 装进包目录，但 setuptools 默认只打包 `.py`，
> 漏了 `package-data` 就只有包没有 `.so`，报错信息会极具误导性。

### 7.3 Windows 侧怎么用

| 侧                     | 做法                                                                       |
|------------------------|----------------------------------------------------------------------------|
| Windows（开发）        | **不编译扩展**。`settings.collector_backend = "stub"`，全部业务逻辑跑 stub |
| openEuler（构建/运行） | `bash deployments/openeuler/build.sh`（产出并安装含 `.so` 的包）          |

> pytest 里用 `pytest.importorskip("industrial_collector")` 或直接走 stub，**不要**在 Windows 上尝试编译（缺
> libmodbus/open62541）。

### 7.4 构建产物的三个必查项

| 检查            | 命令                                      | 失败现象                                   |
|-----------------|-------------------------------------------|--------------------------------------------|
| 扩展能 import   | `python -c "import industrial_collector"` | `ImportError: undefined symbol` → 缺链接库 |
| 依赖完整        | `ldd .../_core*.so \| grep "not found"`   | 出现 `not found` → import 时加载动态库失败 |
| Python ABI 匹配 | 扩展文件名含 `cpython-311`                | 混版本 → `ModuleNotFoundError`             |

---

## 八、Python 与 C++ 的接口清单（可直接作为开发验收表）

| #  | Python 侧                    | C++ 侧                                   | 阻塞           | GIL                  | 返回值                       |
|----|------------------------------|------------------------------------------|----------------|----------------------|------------------------------|
| 1  | `init(cfg)`                  | `Collector::init`                        | 一次           | 释放                 | `None`                       |
| 2  | `apply_plan(plan, revision)` | `Collector::apply_plan(specs, revision)` | 编译期         | 释放（建驱动可阻塞） | `int`（`rc`，0=成功）        |
| 3  | `set_event_callback(cb)`     | `set_event_callback(std::function)`      | 否             | **持有**             | `None`                       |
| 4  | `snapshot_since(since_seq)`  | `DataStore::snapshot_since`              | 否             | 转换时持有           | `(new_seq:int, list[tuple])` |
| 5  | `write_point(dev, pid, val)` | `Collector::write_point`                 | **否**（入队） | 释放                 | `command_id:int`             |
| 6  | `list_protocols()`           | `DriverFactory::listAvailable`           | 否             | 持有                 | `list[str]`                  |
| 6b | `protocol_capabilities()`    | 各驱动 `capability()`                    | 否             | 持有                 | `dict[str, dict]`            |
| 7  | `health()`                   | `Collector::health`                      | 否             | 持有                 | `dict`                       |
| 8  | `shutdown()`                 | `stop + join`                            | **是**         | **必须释放**         | `None`                       |
| 9  | ← 回调 `device_status`       | `emit_status`                            | 否             | **acquire**          | —                            |
| 10 | ← 回调 `write_result`        | `emit_write_result`                      | 否             | **acquire**          | —                            |
| 11 | ← 回调 `plan_ack`            | `emit_plan_ack`                          | 否             | **acquire**          | —                            |
| 12 | ← 回调 `collector_stats`     | `emit_stats`（1s 周期）                  | 否             | **acquire**          | —                            |

> **`apply_plan` 的 revision 由 Python 生成并传入**，扩展只做幂等校验（同 revision 重复下发不重建），不自己发号 ——
> 版本次序号是业务语义，归 Python 的 `runtime_plan_revision` 管理（见 01 号 6.3、02 号 3.x）。

### 8.1 错误码对齐（唯一口径，全文档以此为准）

**先明确一件事**：异常对象上**有 `rc` 和 `code` 两个字段，职责不同** —— 这是消除 01/03/04/05 四份文档此前三种互斥说法（
`code=4000+rc`、按类名映射、`code` 直接是绝对码）的唯一办法。

| 字段   | 谁产生          | 取值                                          | 用途                                                 |
|--------|-----------------|-----------------------------------------------|------------------------------------------------------|
| `rc`   | **C++ 扩展**    | `1~6`（与异常类一一对应）                     | 扩展内部契约；跨语言后仍原样保留，供日志与诊断定位   |
| `code` | **Python 派生** | 绝对业务码 `4001~4006`，即 `code = 4000 + rc` | 直接作为 HTTP 响应体的 `code` 透传，**不再二次映射** |

> 所以 03 号 2.3 的"`code = 4000 + rc`"与本文档的"4001~4006"**不是两套方案，是同一套**：`rc` 是扩展侧相对码，`code` 是
> Python 侧绝对码。此前文案把两者混写成"按类名映射到绝对码"和"透传 4000+rc"两种口径，现已统一为一条流水线：**类 → rc →
> code**。

| rc | C++ 抛出               | Python 注册异常类          | `code` | 触发场景                                           |
|----|------------------------|----------------------------|--------|----------------------------------------------------|
| 1  | `ProtocolNotSupported` | `ProtocolNotSupported`     | 4001   | 协议不在 `list_protocols()` 内                     |
| 2  | `InvalidConnection`    | `InvalidConnection`        | 4002   | 连接参数缺失/非法、`scale=0`、`address` 为空、超限 |
| 3  | `PlanRevisionConflict` | `PlanRevisionConflict`     | 4003   | 同 revision 内容不一致 / 回退 revision             |
| 4  | `NotInitialized`       | `NotInitialized`           | 4004   | 未 `init` 即调用任何采集/控制接口                  |
| 5  | `ControlQueueFull`     | `ControlQueueFull`         | 4005   | 控制队列已满                                       |
| 6  | `DeviceNotInPlan`      | `DeviceNotInPlan`          | 4006   | 写入目标设备/点位不在 plan                         |
| —  | 其它 `std::exception`  | `CollectorError`（`rc=0`） | 500    | 兜底，消息带 C++ `what()`                          |

```cpp
// ---------- 异常定义：rc 是唯一权威，code 不参与 C++ 侧 ----------
class CollectorError : public std::runtime_error {
public:
    CollectorError(int rc, std::string stage, std::string device_key,
                   int64_t point_id, std::string msg)
        : std::runtime_error(std::move(msg)), rc_(rc), stage_(std::move(stage)),
          device_key_(std::move(device_key)), point_id_(point_id) {}
    int         rc()         const noexcept { return rc_; }
    const std::string &stage()      const noexcept { return stage_; }
    const std::string &device_key() const noexcept { return device_key_; }
    int64_t     point_id()   const noexcept { return point_id_; }
private:
    int rc_; std::string stage_, device_key_; int64_t point_id_;
};

class ProtocolNotSupported  : public CollectorError { using CollectorError::CollectorError; };
class InvalidConnection     : public CollectorError { using CollectorError::CollectorError; };
class PlanRevisionConflict  : public CollectorError { using CollectorError::CollectorError; };
class NotInitialized        : public CollectorError { using CollectorError::CollectorError; };
class ControlQueueFull     : public CollectorError { using CollectorError::CollectorError; };
class DeviceNotInPlan       : public CollectorError { using CollectorError::CollectorError; };
```

```cpp
// ---------- 注册：6 个派生类同挂 CollectorError 基类（派生类在前） ----------
py::register_exception<CollectorError>(m, "CollectorError");
py::register_exception<ProtocolNotSupported>(m, "ProtocolNotSupported", py::base<CollectorError>());
py::register_exception<InvalidConnection>   (m, "InvalidConnection",    py::base<CollectorError>());
py::register_exception<PlanRevisionConflict> (m, "PlanRevisionConflict", py::base<CollectorError>());
py::register_exception<NotInitialized>      (m, "NotInitialized",      py::base<CollectorError>());
py::register_exception<ControlQueueFull>    (m, "ControlQueueFull",    py::base<CollectorError>());
py::register_exception<DeviceNotInPlan>     (m, "DeviceNotInPlan",     py::base<CollectorError>());
```

```cpp
// ---------- 属性暴露：把 rc/stage/device_key/point_id 挂到 Python 异常实例上 ----------
static void bind_exception_attrs(py::module_ &m) {
    // pybind11 的 register_exception 返回的异常类可继续加属性，
    // 由 Collector 的方法外层 try/catch 统一包装（见下方 translate()）
}

// 所有对外方法统一走这一层，保证 C++ 线程外绝不泄漏未注册异常
template <class F>
auto guarded(const char *stage, F &&f) {
    try {
        return f();
    } catch (const CollectorError &e) {
        py::object exc = get_registered_exception(e);   // 按 e.rc() 选类
        PyObject *args = Py_BuildValue("(O)", py::str(e.what()).ptr());
        PyObject *inst = PyObject_CallObject(exc.ptr(), args);   // 构造实例
        attach_attrs(inst, e.rc(), e.stage(), e.device_key(), e.point_id());
        PyErr_SetObject(exc.ptr(), inst);
        throw py::error_already_set();
    } catch (const std::exception &e) {
        throw std::runtime_error(e.what());            // rc=0 → 500
    }
}
```

> **`register_exception` 的局限**：pybind11 只能把 C++ 异常**类型 + `what()` 消息**映射到 Python，**不会自动带出 `rc`/
> `stage`/`device_key`/`point_id` 这些自定义字段**。若需要结构化字段（04 号 2.4 强制要求带 `device_key`/`point_id` 定位），必须在
> `guarded()` 里构造实例后手动 `setattr`，或把这些字段拼进 `what()` 的消息文本（**不推荐**，运维要解析字符串）。本文档推荐前者。

```python
# Python 侧统一映射：一处定义，API 层直接透传
RC_TO_CODE = {0: 500, 1: 4001, 2: 4002, 3: 4003, 4: 4004, 5: 4005, 6: 4006}

CLASS_TO_RC = {
    "ProtocolNotSupported": 1, "InvalidConnection": 2, "PlanRevisionConflict": 3,
    "NotInitialized": 4, "ControlQueueFull": 5, "DeviceNotInPlan": 6,
}

def to_api_error(exc: BaseException) -> tuple[int, str, dict]:
    """把扩展异常转成 (code, message, extra)，extra 带定位信息。"""
    rc = getattr(exc, "rc", CLASS_TO_RC.get(type(exc).__name__, 0))
    extra = {k: getattr(exc, k) for k in ("stage", "device_key", "point_id")
             if getattr(exc, k, None) not in (None, "")}
    return RC_TO_CODE.get(rc, 500), str(exc), extra
```

> **为什么不按 Python 异常类名（`ValueError`/`RuntimeError`/`KeyError`）去判别**：本文档此前一度建议"C++ 抛 `KeyError` 映射
> 4006"，这会让**任何** `KeyError`（比如 Python 代码自己字典访问失误）都被误判成扩展错误，是典型的错误分类污染。
> **一律用扩展自己的异常类 + `rc` 判别**，标准库异常只走 500 兜底。

---

## 九、必须补的 12 项（对照你的骨架逐条）

| #  | 缺口                               | 补法                                                        | 严重度                       |
|----|------------------------------------|-------------------------------------------------------------|------------------------------|
| 1  | 每设备一线程                       | 时间轮 + IO 线程池（第四章）                                | **致命**                     |
| 2  | `DataPoint` 内嵌 mutex             | 集中式 `DataStore`（3.2）                                   | 高                           |
| 3  | 单 addr/count                      | `PointSpec` 列表 + `ModbusCodec` 地址解析（3.1、04 号 4.4） | **致命**                     |
| 4  | 无 `scale/offset/dataType`         | 补齐换算与解码（04 号 4.5）                                 | **致命**                     |
| 5  | `start(DataPoint&)` 持 Python 引用 | C++ 内部自有存储                                            | **致命**（野指针）           |
| 6  | 质量码用 `v!=0` 判定               | 用驱动返回的 `StatusCode`（4.6 / 04 号 4.6）                | **致命**（读数 0 被判失败）  |
| 7  | 无异常处理                         | `register_exception` + 异常码表（8.1）                      | **致命**（`std::terminate`） |
| 8  | `stop()` 死锁                      | `gil_scoped_release` 后 join（4.5）                         | **致命**                     |
| 9  | 采样逐点回调                       | 改拉取（3.3 / 5.5）                                         | 高                           |
| 10 | 回调非线程安全                     | `EventDispatcher` 只入队（5.4）                             | 高                           |
| 11 | MQTT 只 publish                    | 拆 `MqttSubscriber`（采集）与 `MqttPublisher`（可留）       | 高（业务错位）               |
| 12 | 手拷 `.so`                         | `build.sh` 的 `cmake --install --prefix python`（第七章）   | 中                           |
| 13 | Paho vs mosquitto 混用             | 统一 mosquitto（04 号 7.1 已实测）                          | 中                           |
| 14 | 无 `list_protocols`                | 加能力查询（04 号第六章）                                   | 中                           |
| 15 | 无 `apply_plan` 校验               | 复刻 `plan_validator`（03 号 6.1 T1~T3）                    | 高                           |

---

## 十、开发落地顺序

| 阶段 | 内容                                                                  | 验证方式                                                         |
|------|-----------------------------------------------------------------------|------------------------------------------------------------------|
| S0   | 项目根 `pyproject.toml` + CMake 骨架 + 空 `_core`（只导出 `health()`） | `bash build.sh` 成功，`import industrial_collector` 通过       |
| S1   | `DataStore` + `init` / `health` / `shutdown`（无线程）                | 单测：init→health→shutdown 无泄漏                                |
| S2   | `ModbusTcpDriver` + 时间轮 + IO 池 + `snapshot_since`                 | ModbusPal 模拟器，读 10 点 × 1s                                  |
| S3   | `apply_plan`（含校验 + 异常码）                                       | 喂非法 plan，验证 4001/4002                                      |
| S4   | `set_event_callback` + `device_status` + `write_point`/`write_result` | 断开模拟器，验证 offline 事件                                    |
| S5   | OPC UA / MQTT / Socket 驱动                                           | 逐协议 + 模拟器                                                  |
| S6   | Python `SamplePump` + ingest COPY + 报警判定                          | 端到端：C++ 采样 → DB → WS                                       |
| S7   | stub 复刻全部校验，Windows 跑全链路                                   | Windows CI 全绿                                                  |
| S8   | 压测 5000 点 × 1s                                                     | CPU/内存/延迟基线                                                |

> **S2 是关键里程碑**：它验证了"时间轮 + 线程池 + 拉取"这套模型真的可行。若 S2 阶段就发现"`snapshot_since`
> 有严重拷贝开销"，前面所有设计要重估 —— 所以**不要先做 5 个驱动再做调度**。

---

## 十一、已定案与仍待确认

### 11.1 已定案（不再讨论，直接照做）

| 编号 | 事项              | 定论                                                                  | 出处      |
|------|-------------------|-----------------------------------------------------------------------|-----------|
| P4   | 绑定形态          | **pybind11 扩展 `industrial_collector._core`**。C ABI + ctypes 已废弃 | 第一章    |
| P5   | 扩展模块名        | **`_core`**。与包名同名会冲突，不可用                                 | 7.2       |
| P1   | MQTT 库           | **libmosquitto 2.0.21**（04 号 7.1 实测）。Paho C 不引入              | 9 章 #13  |
| P2   | 采样取数方式      | **拉取（200ms `snapshot_since`）**，不走回调                          | 3.3 / 5.5 |
| —    | Python 适配层目录 | **`app/core/collector/`**，不再用 `app/core/kernel/`                  | 5.1       |

### 11.2 仍待业务确认

| 编号 | 问题                                                           | 影响                                                       |
|------|----------------------------------------------------------------|------------------------------------------------------------|
| P3   | 是否需要保留 `MqttPublisher`（把采集值再 MQTT 上报给第三方）？ | 与骨架的差异点；不影响 v1 主线                             |
| P6   | `write_point` 是否在扩展内做"点位只读校验"，还是只靠 Python？  | 4006 的触发点（本文档建议**扩展内也校验**，防绕过）        |
| P7   | `protocol_capabilities()` 是 v1 必交付，还是 P2 再说？         | 04 号标注"可选扩展接口"；前端置灰可先读 `list_protocols()` |