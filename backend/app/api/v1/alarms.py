from __future__ import annotations

import time
from typing import Any

from fastapi import APIRouter, Depends, Query

from app.core.deps import get_db_pool, require_login
from app.core.errors import BizError
from app.db.pool import DatabasePool

router = APIRouter()


@router.get("/list")
async def list_alarms(
        page: int = Query(1, ge=1),
        size: int = Query(20, ge=1, le=200),
        state: int | None = Query(None, ge=1, le=3),
        device_code: str | None = None,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    where = ""
    args: list[Any] = []
    if state is not None:
        args.append(state)
        where = f"WHERE state = ${len(args)}"
    if device_code:
        cond = f"device_code = ${len(args) + 1}"
        where = (where + " AND " + cond) if where else "WHERE " + cond
        args.append(device_code)
    rows = await pool.fetch(
        f"SELECT * FROM using_alarms {where} ORDER BY triggered_at DESC "
        f"LIMIT ${len(args) + 1} OFFSET ${len(args) + 2}",
        *args, size, (page - 1) * size,
    )
    total_row = await pool.fetchrow(f"SELECT COUNT(*) AS cnt FROM using_alarms {where}", *args)
    return {"items": [dict(r) for r in rows], "total": int(total_row["cnt"])}


@router.get("/{alarm_id}")
async def get_alarm(alarm_id: int, pool: DatabasePool = Depends(get_db_pool),
                    user: dict = Depends(require_login)) -> dict:
    row = await pool.fetchrow("SELECT * FROM using_alarms WHERE id = $1", alarm_id)
    if row is None:
        raise BizError(code=404, message="alarm not found")
    return dict(row)


@router.put("/{alarm_id}/ack")
async def ack_alarm(alarm_id: int, pool: DatabasePool = Depends(get_db_pool),
                    user: dict = Depends(require_login)) -> dict:
    roles = user.get("roles") or []
    if "operator" not in roles and "engineer" not in roles and "admin" not in roles:
        raise BizError(code=403, message="operator+ role required")
    row = await pool.fetchrow("SELECT state FROM using_alarms WHERE id = $1", alarm_id)
    if row is None:
        raise BizError(code=404, message="alarm not found")
    if int(row["state"]) != 1:
        raise BizError(code=400, message="only active alarms can be acknowledged")
    now = int(time.time())
    await pool.execute(
        "UPDATE using_alarms SET state = 2, acknowledged_at = $1 WHERE id = $2", now, alarm_id,
    )
    return {"ok": True}


@router.put("/{alarm_id}/recover")
async def recover_alarm(alarm_id: int, pool: DatabasePool = Depends(get_db_pool),
                        user: dict = Depends(require_login)) -> dict:
    roles = user.get("roles") or []
    if "operator" not in roles and "engineer" not in roles and "admin" not in roles:
        raise BizError(code=403, message="operator+ role required")
    now = int(time.time())
    await pool.execute(
        "UPDATE using_alarms SET state = 3, recovered_at = $1 WHERE id = $2", now, alarm_id,
    )
    return {"ok": True}


@router.get("/stats")
async def alarm_stats(pool: DatabasePool = Depends(get_db_pool), user: dict = Depends(require_login)) -> dict:
    active = await pool.fetchrow("SELECT COUNT(*) AS cnt FROM using_alarms WHERE state = 1")
    acknowledged = await pool.fetchrow("SELECT COUNT(*) AS cnt FROM using_alarms WHERE state = 2")
    recovered = await pool.fetchrow("SELECT COUNT(*) AS cnt FROM using_alarms WHERE state = 3")
    return {
        "active": int(active["cnt"]),
        "acknowledged": int(acknowledged["cnt"]),
        "recovered": int(recovered["cnt"]),
    }
