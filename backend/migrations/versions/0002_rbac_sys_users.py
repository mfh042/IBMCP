"""rbac sys_users / sys_roles

Revision ID: 0002
Revises: 0001
Create Date: 2026-11-20

对齐 backend/app/api/v1/auth.py 实际使用的 sys_* 表结构。
"""

from __future__ import annotations

import time

from alembic import op
import sqlalchemy as sa

revision = "0002"
down_revision = "0001"
branch_labels = None
depends_on = None


def upgrade() -> None:
    conn = op.get_bind()

    conn.execute(sa.text("""
    CREATE TABLE IF NOT EXISTS sys_roles (
        id          BIGSERIAL PRIMARY KEY,
        role_code   VARCHAR(64)  NOT NULL UNIQUE,
        role_name   VARCHAR(128) NOT NULL,
        description TEXT,
        created_at  BIGINT NOT NULL DEFAULT EXTRACT(EPOCH FROM NOW())::BIGINT,
        updated_at  BIGINT NOT NULL DEFAULT EXTRACT(EPOCH FROM NOW())::BIGINT
    );
    """))

    conn.execute(sa.text("""
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
    """))

    now = int(time.time())
    roles = [
        ("admin", "系统管理员", "全部权限"),
        ("engineer", "工程师", "设备与点位配置、下发控制"),
        ("operator", "操作员", "查看数据、确认报警、有限控制"),
        ("viewer", "观察者", "纯只读"),
    ]
    for code, name, desc in roles:
        conn.execute(
            sa.text(
                "INSERT INTO sys_roles (role_code, role_name, description, created_at, updated_at) "
                "VALUES (:code, :name, :desc, :ts, :ts) ON CONFLICT (role_code) DO NOTHING"
            ),
            {"code": code, "name": name, "desc": desc, "ts": now},
        )

    conn.execute(
        sa.text(
            "INSERT INTO sys_users "
            "(username, display_name, password_hash, roles, enabled, protected, created_at, updated_at) "
            "VALUES (:u, :d, :h, :r, TRUE, TRUE, :ts, :ts) "
            "ON CONFLICT (username) DO NOTHING"
        ),
        {
            "u": "admin",
            "d": "管理员",
            "h": "$argon2id$v=19$m=65536,t=3,p=4$5eVQQywLzV3HbNk4v6eGfg$kuQIwhCGUFfzrJ+z8WcVGKAXs5rBpYP8WNKW3cl8rPI",
            "r": ["admin"],
            "ts": now,
        },
    )


def downgrade() -> None:
    conn = op.get_bind()
    conn.execute(sa.text("DROP TABLE IF EXISTS sys_users;"))
    conn.execute(sa.text("DROP TABLE IF EXISTS sys_roles;"))
