from __future__ import annotations

from typing import Any

from fastapi import APIRouter, Depends, Query

from app.core.collector.protocol import CollectorApi
from app.core.deps import get_collector_backend, get_db_pool, require_login
from app.core.errors import BizError
from app.db.pool import DatabasePool
from app.services.control_service import ControlService

router = APIRouter()


@router.get("")
async def list_devices(
        page: int = Query(1, ge=1),
        size: int = Query(20, ge=1, le=200),
        status: int | None = None,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    where = ""
    args: list[Any] = []
    if status is not None:
        where = "WHERE status = $1"
        args.append(status)
    rows = await pool.fetch(
        f"SELECT * FROM using_device_runtime {where} ORDER BY device_key "
        f"LIMIT ${len(args) + 1} OFFSET ${len(args) + 2}",
        *args, size, (page - 1) * size,
    )
    total_row = await pool.fetchrow(f"SELECT COUNT(*) AS cnt FROM using_device_runtime {where}", *args)
    return {"items": [dict(r) for r in rows], "total": int(total_row["cnt"])}


@router.get("/overview")
async def overview(pool: DatabasePool = Depends(get_db_pool), user: dict = Depends(require_login)) -> dict:
    total_row = await pool.fetchrow("SELECT COUNT(*) AS cnt FROM using_device_runtime")
    online_row = await pool.fetchrow("SELECT COUNT(*) AS cnt FROM using_device_runtime WHERE status = 1")
    alarm_row = await pool.fetchrow("SELECT COUNT(*) AS cnt FROM using_alarms WHERE state = 1")
    return {
        "device_total": int(total_row["cnt"]),
        "device_online": int(online_row["cnt"]),
        "device_offline": int(total_row["cnt"]) - int(online_row["cnt"]),
        "active_alarm_count": int(alarm_row["cnt"]),
    }


@router.get("/{device_code}")
async def get_device(device_code: str, pool: DatabasePool = Depends(get_db_pool),
                     user: dict = Depends(require_login)) -> dict:
    row = await pool.fetchrow("SELECT * FROM using_device_runtime WHERE device_key = $1", device_code)
    if row is None:
        return {"device_key": device_code, "status": 0, "last_error": None, "last_seen": None}
    return dict(row)


@router.get("/{device_code}/points")
async def get_device_points(device_code: str, pool: DatabasePool = Depends(get_db_pool),
                            user: dict = Depends(require_login)) -> dict:
    rows = await pool.fetch(
        "SELECT * FROM using_points WHERE device_code = $1 ORDER BY id", device_code,
    )
    return {"items": [dict(r) for r in rows], "total": len(rows)}


@router.get("/{device_code}/points/{point_id}")
async def get_device_point(device_code: str, point_id: int, pool: DatabasePool = Depends(get_db_pool),
                           user: dict = Depends(require_login)) -> dict:
    row = await pool.fetchrow(
        "SELECT * FROM using_points WHERE device_code = $1 AND id = $2", device_code, point_id,
    )
    if row is None:
        raise BizError(code=404, message="point not found")
    return dict(row)


@router.post("/{device_code}/control")
async def control_device(
        device_code: str,
        body: dict,
        pool: DatabasePool = Depends(get_db_pool),
        collector: CollectorApi = Depends(get_collector_backend),
        user: dict = Depends(require_login),
) -> dict:
    roles = user.get("roles") or []
    if "engineer" not in roles and "admin" not in roles and "operator" not in roles:
        raise BizError(code=403, message="operator+ role required")
    point_id = body.get("point_id")
    value = body.get("value")
    if point_id is None or value is None:
        raise BizError(code=422, message="point_id and value required")
    svc = ControlService(pool, collector)
    return await svc.write(device_code, int(point_id), str(value), point_name=body.get("point_name"))


@router.get("/{device_code}/last-values")
async def last_values(
        device_code: str,
        limit: int = Query(50, ge=1, le=500),
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    rows = await pool.fetch(
        "SELECT t.point_id, t.value, t.quality, t.ts "
        "FROM time_series_data t "
        "JOIN using_points p ON p.id = t.point_id "
        "WHERE p.device_code = $1 "
        "ORDER BY t.ts DESC LIMIT $2",
        device_code, limit,
    )
    grouped: dict[int, dict[str, Any]] = {}
    for r in rows:
        pid = int(r["point_id"])
        if pid not in grouped:
            grouped[pid] = {
                "point_id": pid,
                "value": r["value"],
                "quality": r["quality"],
                "ts": r["ts"],
            }
    return {"items": list(grouped.values()), "device_code": device_code}
