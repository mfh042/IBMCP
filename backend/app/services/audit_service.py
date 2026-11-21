from __future__ import annotations

import json
import logging
import time
from typing import Any

from app.db.pool import DatabasePool

logger = logging.getLogger(__name__)


async def write_audit(
        pool: DatabasePool,
        *,
        level: str,
        target_id: int,
        action: str,
        operator: str | None,
        detail: dict[str, Any] | None = None,
        revision_before: int | None = None,
        revision_after: int | None = None,
) -> None:
    """写入 audit_config_change（02 号 §5.3）。

    审计失败不影响主流程，仅记录日志。
    """
    try:
        await pool.execute(
            "INSERT INTO audit_config_change "
            "(target_level, target_id, action, plan_revision_before, plan_revision_after, "
            " operator, detail, created_at) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7::jsonb, $8)",
            level,
            target_id,
            action,
            revision_before,
            revision_after,
            operator,
            json.dumps(detail or {}, ensure_ascii=False, default=str),
            int(time.time()),
        )
    except Exception:
        logger.exception("write audit failed level=%s target=%s action=%s", level, target_id, action)


def operator_of(user: dict) -> str | None:
    return user.get("username") or user.get("user_name") or user.get("sub")
