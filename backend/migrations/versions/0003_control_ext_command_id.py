"""control_commands.ext_command_id

Revision ID: 0003
Revises: 0002
Create Date: 2026-11-20

扩展是控制命令的唯一发号方（03 号 §3.7），Python 侧需要保存
`write_point()` 返回的 command_id，才能把 `write_result` 事件回写到台账。
"""

from __future__ import annotations

from alembic import op
import sqlalchemy as sa

revision = "0003"
down_revision = "0002"
branch_labels = None
depends_on = None


def upgrade() -> None:
    conn = op.get_bind()
    conn.execute(sa.text(
        "ALTER TABLE control_commands ADD COLUMN IF NOT EXISTS ext_command_id BIGINT"
    ))
    conn.execute(sa.text(
        "CREATE INDEX IF NOT EXISTS idx_commands_ext_id ON control_commands (ext_command_id)"
    ))


def downgrade() -> None:
    conn = op.get_bind()
    conn.execute(sa.text("DROP INDEX IF EXISTS idx_commands_ext_id"))
    conn.execute(sa.text(
        "ALTER TABLE control_commands DROP COLUMN IF EXISTS ext_command_id"
    ))
