# 02 - 数据模型与 ORM 映射

> 数据库保持不变：PostgreSQL 15（Docker 容器），库 `ibmcp`，用户 `ibmcp`。本节说明在 Drogon 中如何访问这些表、如何替代 Alembic 迁移、如何处理 JSONB 与批量 COPY。

## 1. 表清单（最终态）

迁移链 `0001`→`0002`→`0003`→`0004`→`0005` 全部应用后的最终 schema 共 25 张表，分五类（其中 `0002` 新增 `sys_users`/`sys_roles` 并播种 admin，`0004` 删除旧版 `users`/`roles`/`user_roles`，`0003`/`0005` 为增量列）：

### A 类：静态字典表（10）

| 表名 | 说明 | 主键 |
|------|------|------|
| `base_device_category_dict` | 设备类别字典 | id (BIGSERIAL) |
| `base_device_brand_model_dict` | 品牌型号字典 | id |
| `base_device_port_dict` | 端口字典 | id |
| `base_device_protocol_dict` | 协议字典 | id |
| `base_device_base_info` | 设备主数据（含 `supported_ports` varchar、`data_items` text、`supported_protocols` varchar） | id |
| `base_device_data_item_dict` | 数据项字典 | id |
| `base_process_route_resource` | 工艺路线资源 | id |
| `base_process_route_step` | 工艺路线步骤 | id |
| `base_equipment_base_info` | 装备主数据 | id |
| `base_equipment_function_dict` | 装备功能字典 | id |

### B 类：运行动态配置表（5）

| 表名 | 说明 |
|------|------|
| `running_equipment_config` | 装备运行配置（五级链顶层） |
| `running_equipment_route` | 配置下挂路线 |
| `running_equipment_function` | 配置下挂功能 |
| `running_route_device` | 路线步骤绑定设备 |
| `running_device_collection` | 设备绑定数据项（采集点） |

### C 类：运行期结果与快照（5）

| 表名 | 说明 | 主键 |
|------|------|------|
| `using_devices` | 在用设备台账 | id |
| `using_points` | 在用测点台账 | id |
| `time_series_data` | 时序数据 | 复合主键 `(ts, point_id)` |
| `using_alarms` | 报警记录 | id |
| `control_commands` | 控制命令台账（含 `ext_command_id BIGINT` 及索引） | id |

### D 类：RBAC（2）

| 表名 | 说明 | 主键 |
|------|------|------|
| `sys_roles` | 角色字典（`role_code` / `role_name` / `description`） | id |
| `sys_users` | 用户（`username`、`password_hash`、`display_name`、`roles TEXT[]`、`protected`、`enabled`，无 `last_login_at`） | id |

> 旧版多对多表 `roles` / `users` / `user_roles` 已由迁移 `0004` 删除；用户-角色关系改为 `sys_users.roles` 数组列，`admin` 等内置账户用 `protected=true` 标记禁止删除。

### E 类：重构新增（3）

| 表名 | 说明 | 主键 |
|------|------|------|
| `runtime_plan_revision` | 采集计划版本（幂等下发依据） | id |
| `audit_config_change` | 配置变更审计 | id |
| `using_device_runtime` | 设备运行态快照（无锁覆盖写） | `device_key` varchar(96) |

索引：`time_series_data(point_id)`、`time_series_data(ts DESC)`、`using_alarms(device_code, state)`、`control_commands(status)`、`control_commands(ext_command_id)`、`runtime_plan_revision(plan_hash/status)`、`audit_config_change(target_level,target_id)`、`audit_config_change(created_at DESC)`、`using_devices(status)`、`using_device_runtime(status)`。

## 2. Drogon Model 生成策略

> **现状（以代码为准）**：项目当前**未**使用 `drogon_ctl create model` 生成 ORM 类——`backend/` 下不存在 `models/` 目录，也无任何生成的 Model 类。全部数据访问通过 `db::client()` / `db::writeClient()`（封装于 [backend/src/db/db.cpp](file:///e:/999-Dev/01-IBMCP-黑盒管控系统/02-IBMCP-drogon/backend/src/db/db.cpp)，返回 `default` / `write` 连接池的 `DbClient`），再调用 `execSqlCoro` 执行裸 SQL（全库 138 处，分布于 controller / service / collector）。下文 §2.1–2.4 为**早期设计策略**，留作参考；若未来引入 ORM 再按此执行。

Drogon 提供 `drogon_ctl create model <dsn> <table>` 从表结构生成 ORM 类（`.h` / `.cc`），生成到 `backend/models/`。但本项目有三种非标准情况，需区别处理（以下为设计参考，当前未实施）：

### 2.1 单 BIGSERIAL 主键表 → 直接生成

23 张表为单 `BIGSERIAL` 主键，直接生成：

```bash
# 在欧拉上，PG 已启动后
cd backend
drogon_ctl create model "host=127.0.0.1 port=5432 dbname=ibmcp user=ibmcp password=ibmcp" base_device_category_dict
# 对其余 22 张表逐一执行（可用脚本批量）
```

生成的 Model 类（如 `BaseDeviceCategoryDict`）自带 `Mapper<T>` 增删改查，`findByPrimaryKey`、`findOne`、`insert`、`update`、`deleteByPrimaryKey`，均支持回调与 C++20 协程。

### 2.2 复合主键表 → 手写 Model 或只用裸 SQL

- 仅 `time_series_data(ts, point_id)` 为复合主键（旧 `user_roles` 已随 `0004` 删除）。`drogon_ctl` 对复合主键支持有限，建议不生成 Model，改用 `db->execSqlCoro(...)` 裸 SQL 操作；该表写入频繁，裸 SQL + COPY 性能更好（见 §5、§6）。

### 2.3 非 BIGINT 主键表 → 生成后微调类型

`using_device_runtime` 主键是 `device_key VARCHAR(96)`，`drogon_ctl` 会生成对应字段；需确认 `Mapper` 主键模板参数为 `std::string`。

### 2.4 JSONB 列的处理

以下列是 `JSONB`：`base_process_route_step.step_params`、`running_equipment_route.route_params`、`running_equipment_function.function_params`、`running_route_device.device_connection`、`audit_config_change.detail`、`using_devices.device_connection`。

Drogon Model 默认把 JSONB 映射为 `std::string`（存原始 JSON 文本）。项目层用 nlohmann/json 序列化/反序列化：

```cpp
// 写入
nlohmann::json j = {{"host", "192.168.1.10"}, {"port", 502}};
device.setDeviceConnection(j.dump());
mapper.insert(device);

// 读取
nlohmann::json conn = nlohmann::json::parse(device.getDeviceConnection());
```

## 3. DbClient 配置

在 `backend/config.json` 的 `db_clients` 中声明两个 Drogon 原生客户端（`backend/config.json:21`）：

```json
{
  "db_clients": [
    {
      "name": "default",
      "rdbms": "postgresql",
      "host": "127.0.0.1",
      "port": 5432,
      "dbname": "ibmcp",
      "user": "ibmcp",
      "passwd": "ibmcp",
      "is_fast": false,
      "number_of_connections": 5
    },
    {
      "name": "write",
      "rdbms": "postgresql",
      "host": "127.0.0.1",
      "port": 5432,
      "dbname": "ibmcp",
      "user": "ibmcp",
      "passwd": "ibmcp",
      "is_fast": false,
      "number_of_connections": 5
    }
  ]
}
```

说明：
- 声明了两个具名客户端：`default`（读）与 `write`（写），当前都指向同一本地 PG；分离命名是为了将来接入只读副本时只需改 `default` 的连接串，业务代码无需改动；
- `number_of_connections`：每个池 5 条连接（开发期默认值）；
- `is_fast=false`：使用完整功能客户端（支持 prepared statement）；
- 业务代码统一通过 `backend/include/db/db.h` 的 `ibmcp::db::ro()` / `ibmcp::db::rw()` 获取客户端（实现见 `backend/src/db/db.cpp:7`，内部按具名 `default`/`write` 取 client），不直接调用 `app().getDbClient()`。

### 3.1 密码不写死在配置文件

生产环境密码/密钥应从环境变量读取。做法：`main.cpp` 启动时读取 `config.json`，若检测到 `${PG_PASSWORD}` 占位符则替换为环境变量值，或直接用代码动态创建 DbClient：

```cpp
auto db = drogon::DbClient::newPgClient(
    fmt::format("host={} port={} dbname={} user={} password={}",
                cfg.host, cfg.port, cfg.dbname, cfg.user, cfg.password),
    cfg.pool_size);
drogon::app().addDbClient(db);
```

推荐后者，统一由自定义 `Config` 类管理所有配置，`config.json` 只放 Drogon 框架级参数（监听、线程、日志）。

> 现状：`backend/src/core/config.cpp` 目前仍从 `config.json` 的 `custom_config` 读取 `app_secret_key`（默认 `dev-secret-change-me`）与 `pg_dsn`，尚未接入环境变量覆盖；上述为生产加固建议，上线前需补齐。

## 4. 异步与协程用法

Drogon Controller 方法若声明为 `drogon::Task<T>`（需 C++20），可直接 `co_await` DbClient：

```cpp
using namespace drogon;

Task<HttpResponsePtr> AuthController::login(HttpRequestPtr req) {
    auto body = req->getJsonObject();  // nlohmann/json 风格
    std::string username = (*body)["username"].asString();

    auto db = app().getDbClient();
    try {
        auto r = co_await db->execSqlCoro(
            "SELECT id, password_hash, roles, enabled FROM sys_users WHERE username = $1",
            username);
        if (r.size() == 0) {
            co_return makeErrorResponse(401, "用户不存在");
        }
        // ... 校验密码、签发 JWT
    } catch (const DrogonDbException &e) {
        LOG_ERROR << "DB error: " << e.base().what();
        co_return makeErrorResponse(500, "数据库错误");
    }
}
```

对应旧 `async def` 中的 `await db.fetchval(...)`。

不使用协程时也可用回调风格 `db->execSql(sql, callback)`，但 Controller 会变复杂；**统一采用 C++20 协程**，编译选项加 `-std=c++20`。

## 5. 迁移策略（替代 Alembic）

Drogon 不内置迁移工具。采用 **「SQL 脚本 + 版本表 + 启动期迁移器」** 方案：

### 5.1 迁移脚本目录

```
backend/migrations/
├── 0001_initial_schema.sql              # 由 0001_initial_schema.py 翻译（纯 SQL）
├── 0002_rbac_sys_users.sql              # sys_users/sys_roles 建表 + 播种 admin（必含，否则无法登录）
├── 0003_control_ext_command_id.sql      # control_commands.ext_command_id + 索引
├── 0004_drop_legacy_rbac.sql            # 删除旧版 users/roles/user_roles（与 0001 配套）
├── 0005_device_supported_ports_items.sql
└── ...后续变更
```

把现有 Python 迁移中的 `conn.execute(sa.text("..."))` 直接提取为纯 SQL（剥离 SQLAlchemy 包装），保持语句与现有库完全一致，保证存量数据无缝衔接。注意两点：

- `0002` 除建 `sys_users`/`sys_roles` 外，还含一条 `INSERT` 播种 `admin`（固定 argon2id 哈希，密码 `admin123`），必须一并翻译，否则 C++ 版首启无账户可登录；
- `0001`（建旧 `users`/`roles`/`user_roles`）与 `0004`（删旧表）成对出现：若面向全新库，也可把两者合并为最终态 SQL（只建 `sys_*`、不建旧表），但**保留全 5 个脚本**可与 Alembic 版本号一一对应，便于对已部署存量库做增量升级。

### 5.2 版本表

```sql
CREATE TABLE IF NOT EXISTS schema_migrations (
    version     VARCHAR(32) PRIMARY KEY,
    applied_at  BIGINT NOT NULL DEFAULT EXTRACT(EPOCH FROM NOW())::BIGINT
);
```

### 5.3 启动期迁移器

`backend/src/db/migrator.cpp`，在 `main.cpp` 中 `app().run()` 之前调用：

```cpp
class Migrator {
public:
    explicit Migrator(const std::string& migrationsDir, const std::string& dsn)
      : dir_(migrationsDir), conn_(dsn) {}   // 用 libpq PQconnectdb 直连（不走连接池）

    void migrate() {
        // 1. 确保 schema_migrations 存在
        // 2. 读取 dir_ 下所有 *.sql，按文件名排序
        // 3. 对每个文件，查 schema_migrations 是否已应用；未应用则在单事务内执行整个文件内容
        // 4. 插入 version 记录
    }
};
```

要点：
- 迁移用独立 libpq 连接（同步），不依赖 Drogon 连接池，确保在服务监听前完成；
- 每个迁移文件整体一个事务，失败回滚且不写版本记录；
- 与旧 Alembic `alembic upgrade head` 行为对齐：幂等、按版本顺序；
- 首次部署到已有库时，需先把当前版本号手动写入 `schema_migrations`（避免对已存在表重复建表，因为脚本用了 `CREATE TABLE IF NOT EXISTS`，实际重复执行也是安全的，但仍建议记录基线）。

### 5.4 回滚

保留每个 `*.sql` 的 `down` 语义可选：可在文件头部用注释块标注 downgrade SQL，或单独维护 `migrations/down/0001.sql`。初期不强制自动回滚，回滚由 DBA 手工执行对应 down 脚本。

## 6. 批量 COPY 入库（时序数据）

`time_series_data` 写入量最大（200ms 泵，千点级），不能用逐条 INSERT。Drogon `DbClient` 不直接封装 COPY，需在底层用 libpq 的 COPY API：

```cpp
// SamplePump 中批量入库
void SamplePump::batchInsert(const std::vector<SampleRow>& rows) {
    auto conn = db_->getConnection();  // 从池借一个连接
    PGconn* pg = conn->pgConn();       // Drogon PgConnection 暴露底层 PGconn*（需确认/扩展）
    PQexec(pg, "COPY time_series_data(ts, point_id, value, quality, raw_value) FROM STDIN");
    for (auto& r : rows) {
        std::string line = fmt::format("{}\t{}\t{}\t{}\t\\N\n",
            toIsoTs(r.ts), r.pointId, r.value, r.quality);
        PQputCopyData(pg, line.data(), (int)line.size());
    }
    PQputCopyEnd(pg, nullptr);
    auto res = PQgetResult(pg);
    // 检查 COPY 结果，归还连接
}
```

> Drogon 的 `PgConnection` 是否暴露 `PGconn*`：默认不直接暴露。两种解法：
> 1. 在 Drogon 源码上打补丁/子类化 `PgConnection` 暴露原生句柄（推荐，最干净）；
> 2. 为 COPY 单独开一个 libpq 连接（不走 Drogon 池），独立连接串，专用于批量写入。
>
> 本项目采用方案 2 的简化版：`SamplePump` 持有一个专用 libpq 连接（同步），与 Drogon 连接池隔离，避免阻塞池内连接。该连接仅在泵线程内使用，天然线程安全。

与旧方案（asyncpg `copy_records_to_table`）等价，性能持平或更优（无 Python 序列化开销）。

## 7. 时区与时间约定

- 所有时间戳列存 `TIMESTAMPTZ`（`time_series_data.ts`）或 `BIGINT` epoch 秒（`created_at` 等），与旧库完全一致；
- 写入 `TIMESTAMPTZ` 时传 ISO 字符串（带 `+08`），由 PG 统一转 UTC 存储；
- 进程时区 `TZ=Asia/Shanghai`（systemd 单元或容器环境变量），与旧部署一致；
- 采样时间戳由采集器产生（`snapshot_since` 带回的 `ts`），业务层不再二次生成，避免时钟漂移。

## 8. 配置项对照（旧 .env → 新 config.json / Config 类）

| 旧环境变量 | 新位置 | 说明 |
|-----------|--------|------|
| `APP_HOST` / `APP_PORT` | `config.json` listeners | Drogon 监听地址端口 |
| `APP_SECRET_KEY` | `Config` 类（环境变量 `APP_SECRET_KEY`） | JWT 签名密钥，不写配置文件 |
| `ACCESS_TOKEN_EXPIRE_MINUTES` | `Config` 类 | 默认 1440 |
| `PG_HOST/PORT/USER/PASSWORD/DATABASE` | `Config` 类 / 环境变量 | 构造 DbClient DSN |
| `PG_POOL_MIN/MAX` | `config.json` `number_of_connections`（取 MAX） | 最小连接由 Drogon 懒加载 |
| `REDIS_URL` | `config.json` redis_clients / `Config` 类 | 见 04/05 |
| `CORS_ORIGINS` | `Config` 类 | CorsFilter 白名单 |
| `COLLECTOR_BACKEND` | **删除** | 不再有 stub/native 之分，统一 native C++ |