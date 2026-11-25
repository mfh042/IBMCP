-- 0002 rbac sys_users / sys_roles
-- 对齐 auth 实际使用的 sys_* 表结构。

CREATE TABLE IF NOT EXISTS sys_roles (
    id          BIGSERIAL PRIMARY KEY,
    role_code   VARCHAR(64)  NOT NULL UNIQUE,
    role_name   VARCHAR(128) NOT NULL,
    description TEXT,
    created_at  BIGINT NOT NULL DEFAULT EXTRACT(EPOCH FROM NOW())::BIGINT,
    updated_at  BIGINT NOT NULL DEFAULT EXTRACT(EPOCH FROM NOW())::BIGINT
);

CREATE TABLE IF NOT EXISTS sys_users (
    id            BIGSERIAL PRIMARY KEY,
    username      VARCHAR(64)  NOT NULL UNIQUE,
    display_name  VARCHAR(128),
    password_hash VARCHAR(255) NOT NULL,
    roles         TEXT[] NOT NULL DEFAULT '{}',
    enabled       BOOLEAN NOT NULL DEFAULT TRUE,
    protected     BOOLEAN NOT NULL DEFAULT FALSE,
    created_at    BIGINT NOT NULL DEFAULT EXTRACT(EPOCH FROM NOW())::BIGINT,
    updated_at    BIGINT NOT NULL DEFAULT EXTRACT(EPOCH FROM NOW())::BIGINT
);

INSERT INTO sys_roles (role_code, role_name, description, created_at, updated_at) VALUES
    ('admin', '系统管理员', '全部权限', EXTRACT(EPOCH FROM NOW())::BIGINT, EXTRACT(EPOCH FROM NOW())::BIGINT),
    ('engineer', '工程师', '设备与点位配置、下发控制', EXTRACT(EPOCH FROM NOW())::BIGINT, EXTRACT(EPOCH FROM NOW())::BIGINT),
    ('operator', '操作员', '查看数据、确认报警、有限控制', EXTRACT(EPOCH FROM NOW())::BIGINT, EXTRACT(EPOCH FROM NOW())::BIGINT),
    ('viewer', '观察者', '纯只读', EXTRACT(EPOCH FROM NOW())::BIGINT, EXTRACT(EPOCH FROM NOW())::BIGINT)
ON CONFLICT (role_code) DO NOTHING;

INSERT INTO sys_users
    (username, display_name, password_hash, roles, enabled, protected, created_at, updated_at)
VALUES
    ('admin', '管理员',
     '$argon2id$v=19$m=65536,t=3,p=4$5eVQQywLzV3HbNk4v6eGfg$kuQIwhCGUFfzrJ+z8WcVGKAXs5rBpYP8WNKW3cl8rPI',
     '{admin}', TRUE, TRUE,
     EXTRACT(EPOCH FROM NOW())::BIGINT, EXTRACT(EPOCH FROM NOW())::BIGINT)
ON CONFLICT (username) DO NOTHING;