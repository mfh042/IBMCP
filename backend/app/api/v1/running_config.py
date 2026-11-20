from __future__ import annotations

import logging
import time
from typing import Any

from fastapi import APIRouter, Depends, Query

from app.core.collector.protocol import CollectorApi
from app.core.deps import get_collector_backend, get_db_pool, require_login
from app.core.errors import BizError
from app.db.pool import DatabasePool
from app.services.collector_orchestrator import CollectorOrchestrator

logger = logging.getLogger(__name__)
router = APIRouter()


@router.get("/equipments")
async def list_configs(
        page: int = Query(1, ge=1),
        size: int = Query(20, ge=1, le=200),
        keyword: str | None = None,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    where = ""
    args: list[Any] = []
    if keyword:
        where = "WHERE config_code ILIKE $1 OR config_name ILIKE $1"
        args.append(f"%{keyword}%")
    rows = await pool.fetch(
        f"SELECT * FROM running_equipment_config {where} ORDER BY id DESC "
        f"LIMIT ${len(args) + 1} OFFSET ${len(args) + 2}",
        *args, size, (page - 1) * size,
    )
    total_row = await pool.fetchrow(
        f"SELECT COUNT(*) AS cnt FROM running_equipment_config {where}", *args
    )
    return {"items": [dict(r) for r in rows], "total": int(total_row["cnt"])}


@router.get("/equipments/{config_id}")
async def get_config(config_id: int, pool: DatabasePool = Depends(get_db_pool),
                     user: dict = Depends(require_login)) -> dict:
    row = await pool.fetchrow("SELECT * FROM running_equipment_config WHERE id = $1", config_id)
    if row is None:
        raise BizError(code=404, message="config not found")
    return dict(row)


@router.get("/equipments/{config_id}/status")
async def config_status(config_id: int, pool: DatabasePool = Depends(get_db_pool),
                        user: dict = Depends(require_login)) -> dict:
    cfg = await pool.fetchrow("SELECT * FROM running_equipment_config WHERE id = $1", config_id)
    if cfg is None:
        raise BizError(code=404, message="config not found")
    last_rev = await pool.fetchrow(
        "SELECT revision, plan_hash, status, applied_at FROM runtime_plan_revision "
        "WHERE config_id = $1 OR config_id IS NULL "
        "ORDER BY revision DESC LIMIT 1",
        config_id,
    )
    route_count = await pool.fetchrow("SELECT COUNT(*) AS cnt FROM running_equipment_route WHERE config_id = $1",
                                      config_id)
    device_count = await pool.fetchrow(
        "SELECT COUNT(*) AS cnt FROM running_route_device r "
        "JOIN running_equipment_route e ON e.id = r.route_config_id WHERE e.config_id = $1 AND r.enabled = 1",
        config_id,
    )
    point_count = await pool.fetchrow(
        "SELECT COUNT(*) AS cnt FROM running_device_collection c "
        "JOIN running_route_device r ON r.id = c.binding_id "
        "JOIN running_equipment_route e ON e.id = r.route_config_id "
        "WHERE e.config_id = $1 AND c.enabled = 1",
        config_id,
    )
    return {
        "config": dict(cfg),
        "last_revision": dict(last_rev) if last_rev else None,
        "route_count": int(route_count["cnt"]),
        "device_count": int(device_count["cnt"]),
        "point_count": int(point_count["cnt"]),
    }


@router.get("/equipments/{config_id}/expand")
async def expand_config(config_id: int, pool: DatabasePool = Depends(get_db_pool),
                        user: dict = Depends(require_login)) -> dict:
    config = await pool.fetchrow("SELECT * FROM running_equipment_config WHERE id = $1", config_id)
    if config is None:
        raise BizError(code=404, message="config not found")
    routes = await pool.fetch(
        "SELECT * FROM running_equipment_route WHERE config_id = $1 ORDER BY id", config_id,
    )
    expanded_routes = []
    for r in routes:
        bindings = await pool.fetch(
            "SELECT * FROM running_route_device WHERE route_config_id = $1 ORDER BY id", r["id"],
        )
        expanded_bindings = []
        for b in bindings:
            collections = await pool.fetch(
                "SELECT * FROM running_device_collection WHERE binding_id = $1 ORDER BY id", b["id"],
            )
            expanded_bindings.append({**dict(b), "collections": [dict(c) for c in collections]})
        expanded_routes.append({**dict(r), "devices": expanded_bindings})
    functions = await pool.fetch(
        "SELECT * FROM running_equipment_function WHERE config_id = $1 ORDER BY id", config_id,
    )
    return {
        **dict(config),
        "routes": expanded_routes,
        "functions": [dict(f) for f in functions],
    }


@router.get("/equipments/{config_id}/plan")
async def preview_plan(
        config_id: int,
        pool: DatabasePool = Depends(get_db_pool),
        collector: CollectorApi = Depends(get_collector_backend),
        user: dict = Depends(require_login),
) -> dict:
    orchestrator = CollectorOrchestrator(pool, collector)
    return await orchestrator.compile_only(config_id)


@router.get("/revisions")
async def list_revisions(
        page: int = Query(1, ge=1),
        size: int = Query(20, ge=1, le=200),
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    rows = await pool.fetch(
        "SELECT * FROM runtime_plan_revision ORDER BY revision DESC LIMIT $1 OFFSET $2",
        size, (page - 1) * size,
    )
    total = await pool.fetchrow("SELECT COUNT(*) AS cnt FROM runtime_plan_revision")
    return {"items": [dict(r) for r in rows], "total": int(total["cnt"])}


@router.post("/equipments")
async def create_config(body: dict, pool: DatabasePool = Depends(get_db_pool),
                        user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    now = int(time.time())
    data = {**body, "config_status": 0, "run_status": 0, "created_at": now, "updated_at": now}
    cols = ", ".join(data.keys())
    placeholders = ", ".join(f"${i + 1}" for i in range(len(data)))
    row = await pool.fetchrow(
        f"INSERT INTO running_equipment_config ({cols}) VALUES ({placeholders}) RETURNING *",
        *data.values(),
    )
    return dict(row)


@router.put("/equipments/{config_id}")
async def update_config(config_id: int, body: dict, pool: DatabasePool = Depends(get_db_pool),
                        user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    row = await pool.fetchrow("SELECT id FROM running_equipment_config WHERE id = $1", config_id)
    if row is None:
        raise BizError(code=404, message="config not found")
    now = int(time.time())
    data = {**body, "updated_at": now}
    updates = ", ".join(f"{k} = ${i + 1}" for i, k in enumerate(data.keys()))
    vals = list(data.values()) + [config_id]
    updated = await pool.fetchrow(
        f"UPDATE running_equipment_config SET {updates} WHERE id = ${len(vals)} RETURNING *",
        *vals,
    )
    return dict(updated)


@router.post("/equipments/{config_id}/activate")
async def activate_config(
        config_id: int,
        pool: DatabasePool = Depends(get_db_pool),
        collector: CollectorApi = Depends(get_collector_backend),
        user: dict = Depends(require_login),
) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    orchestrator = CollectorOrchestrator(pool, collector)
    return await orchestrator.activate_config(config_id)


@router.post("/equipments/{config_id}/unload")
async def unload_config(config_id: int, pool: DatabasePool = Depends(get_db_pool),
                        user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    await pool.execute(
        "UPDATE running_equipment_config SET config_status = 0, updated_at = $1 WHERE id = $2",
        int(time.time()), config_id,
    )
    return {"ok": True, "message": "config deactivated (recompile full plan to remove devices from collector)"}


@router.post("/equipments/recompile")
async def recompile_all(
        pool: DatabasePool = Depends(get_db_pool),
        collector: CollectorApi = Depends(get_collector_backend),
        user: dict = Depends(require_login),
) -> dict:
    if "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="admin required")
    orchestrator = CollectorOrchestrator(pool, collector)
    return await orchestrator.recompile_all()


@router.delete("/equipments/{config_id}")
async def delete_config(config_id: int, pool: DatabasePool = Depends(get_db_pool),
                        user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    row = await pool.fetchrow("SELECT config_status FROM running_equipment_config WHERE id = $1", config_id)
    if row is None:
        raise BizError(code=404, message="config not found")
    if int(row["config_status"]) == 1:
        raise BizError(code=3001, message="config is active, unload first")
    await pool.execute("DELETE FROM running_equipment_function WHERE config_id = $1", config_id)
    await pool.execute(
        "DELETE FROM running_route_device WHERE route_config_id IN "
        "(SELECT id FROM running_equipment_route WHERE config_id = $1)",
        config_id,
    )
    await pool.execute("DELETE FROM running_equipment_route WHERE config_id = $1", config_id)
    await pool.execute("DELETE FROM running_equipment_config WHERE id = $1", config_id)
    return {"ok": True}


@router.post("/equipments/{config_id}/sync")
async def sync_to_using(config_id: int, pool: DatabasePool = Depends(get_db_pool),
                        user: dict = Depends(require_login)) -> dict:
    if "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="admin required")
    try:
        await pool.execute("SELECT sync_config_to_using_tables($1)", config_id)
    except Exception as exc:
        raise BizError(code=3002, message=f"sync failed: {exc}") from exc
    return {"ok": True}


@router.delete("/equipments/{config_id}/cleanup")
async def cleanup_from_using(config_id: int, pool: DatabasePool = Depends(get_db_pool),
                             user: dict = Depends(require_login)) -> dict:
    if "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="admin required")
    try:
        await pool.execute("SELECT cleanup_config_from_using_tables($1)", config_id)
    except Exception as exc:
        raise BizError(code=3002, message=f"cleanup failed: {exc}") from exc
    return {"ok": True}


@router.get("/routes")
async def list_routes(
        config_id: int | None = None,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    where = ""
    args: list[Any] = []
    if config_id is not None:
        where = "WHERE config_id = $1"
        args.append(config_id)
    rows = await pool.fetch(
        f"SELECT * FROM running_equipment_route {where} ORDER BY id", *args,
    )
    return {"items": [dict(r) for r in rows], "total": len(rows)}


@router.post("/routes")
async def create_route(body: dict, pool: DatabasePool = Depends(get_db_pool),
                       user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    now = int(time.time())
    data = {**body, "created_at": now, "updated_at": now}
    cols = ", ".join(data.keys())
    ph = ", ".join(f"${i + 1}" for i in range(len(data)))
    row = await pool.fetchrow(f"INSERT INTO running_equipment_route ({cols}) VALUES ({ph}) RETURNING *", *data.values())
    return dict(row)


@router.put("/routes/{route_id}")
async def update_route(route_id: int, body: dict, pool: DatabasePool = Depends(get_db_pool),
                       user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    now = int(time.time())
    data = {**body, "updated_at": now}
    sets = ", ".join(f"{k} = ${i + 1}" for i, k in enumerate(data.keys()))
    vals = list(data.values()) + [route_id]
    row = await pool.fetchrow(
        f"UPDATE running_equipment_route SET {sets} WHERE id = ${len(vals)} RETURNING *", *vals,
    )
    if row is None:
        raise BizError(code=404, message="route not found")
    return dict(row)


@router.delete("/routes/{route_id}")
async def delete_route(route_id: int, pool: DatabasePool = Depends(get_db_pool),
                       user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    await pool.execute(
        "DELETE FROM running_device_collection WHERE binding_id IN (SELECT id FROM running_route_device WHERE route_config_id = $1)",
        route_id)
    await pool.execute("DELETE FROM running_route_device WHERE route_config_id = $1", route_id)
    await pool.execute("DELETE FROM running_equipment_route WHERE id = $1", route_id)
    return {"ok": True}


@router.get("/devices")
async def list_device_bindings(
        route_id: int | None = None,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    where = ""
    args: list[Any] = []
    if route_id is not None:
        where = "WHERE route_config_id = $1"
        args.append(route_id)
    rows = await pool.fetch(
        f"SELECT * FROM running_route_device {where} ORDER BY id", *args,
    )
    return {"items": [dict(r) for r in rows], "total": len(rows)}


@router.post("/devices")
async def create_device_binding(body: dict, pool: DatabasePool = Depends(get_db_pool),
                                user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    now = int(time.time())
    data = {**body, "created_at": now, "updated_at": now}
    cols = ", ".join(data.keys())
    ph = ", ".join(f"${i + 1}" for i in range(len(data)))
    row = await pool.fetchrow(f"INSERT INTO running_route_device ({cols}) VALUES ({ph}) RETURNING *", *data.values())
    return dict(row)


@router.put("/devices/{binding_id}")
async def update_device_binding(binding_id: int, body: dict, pool: DatabasePool = Depends(get_db_pool),
                                user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    now = int(time.time())
    data = {**body, "updated_at": now}
    sets = ", ".join(f"{k} = ${i + 1}" for i, k in enumerate(data.keys()))
    vals = list(data.values()) + [binding_id]
    row = await pool.fetchrow(
        f"UPDATE running_route_device SET {sets} WHERE id = ${len(vals)} RETURNING *", *vals,
    )
    if row is None:
        raise BizError(code=404, message="device binding not found")
    return dict(row)


@router.delete("/devices/{binding_id}")
async def delete_device_binding(binding_id: int, pool: DatabasePool = Depends(get_db_pool),
                                user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    await pool.execute("DELETE FROM running_device_collection WHERE binding_id = $1", binding_id)
    await pool.execute("DELETE FROM running_route_device WHERE id = $1", binding_id)
    return {"ok": True}


@router.get("/points")
async def list_collections(
        binding_id: int | None = None,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    where = ""
    args: list[Any] = []
    if binding_id is not None:
        where = "WHERE binding_id = $1"
        args.append(binding_id)
    rows = await pool.fetch(
        f"SELECT * FROM running_device_collection {where} ORDER BY id", *args,
    )
    return {"items": [dict(r) for r in rows], "total": len(rows)}


@router.post("/points")
async def create_collection(body: dict, pool: DatabasePool = Depends(get_db_pool),
                            user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    now = int(time.time())
    data = {**body, "created_at": now, "updated_at": now}
    cols = ", ".join(data.keys())
    ph = ", ".join(f"${i + 1}" for i in range(len(data)))
    row = await pool.fetchrow(f"INSERT INTO running_device_collection ({cols}) VALUES ({ph}) RETURNING *",
                              *data.values())
    return dict(row)


@router.put("/points/{point_id}")
async def update_collection(point_id: int, body: dict, pool: DatabasePool = Depends(get_db_pool),
                            user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    now = int(time.time())
    data = {**body, "updated_at": now}
    sets = ", ".join(f"{k} = ${i + 1}" for i, k in enumerate(data.keys()))
    vals = list(data.values()) + [point_id]
    row = await pool.fetchrow(
        f"UPDATE running_device_collection SET {sets} WHERE id = ${len(vals)} RETURNING *", *vals,
    )
    if row is None:
        raise BizError(code=404, message="collection not found")
    return dict(row)


@router.delete("/points/{point_id}")
async def delete_collection(point_id: int, pool: DatabasePool = Depends(get_db_pool),
                            user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    await pool.execute("DELETE FROM running_device_collection WHERE id = $1", point_id)
    return {"ok": True}


@router.get("/functions")
async def list_functions(
        config_id: int | None = None,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    where = ""
    args: list[Any] = []
    if config_id is not None:
        where = "WHERE config_id = $1"
        args.append(config_id)
    rows = await pool.fetch(
        f"SELECT * FROM running_equipment_function {where} ORDER BY id", *args,
    )
    return {"items": [dict(r) for r in rows], "total": len(rows)}


@router.post("/functions")
async def create_function(body: dict, pool: DatabasePool = Depends(get_db_pool),
                          user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    now = int(time.time())
    data = {**body, "created_at": now, "updated_at": now}
    cols = ", ".join(data.keys())
    ph = ", ".join(f"${i + 1}" for i in range(len(data)))
    row = await pool.fetchrow(f"INSERT INTO running_equipment_function ({cols}) VALUES ({ph}) RETURNING *",
                              *data.values())
    return dict(row)


@router.put("/functions/{func_id}")
async def update_function(func_id: int, body: dict, pool: DatabasePool = Depends(get_db_pool),
                          user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    now = int(time.time())
    data = {**body, "updated_at": now}
    sets = ", ".join(f"{k} = ${i + 1}" for i, k in enumerate(data.keys()))
    vals = list(data.values()) + [func_id]
    row = await pool.fetchrow(
        f"UPDATE running_equipment_function SET {sets} WHERE id = ${len(vals)} RETURNING *", *vals,
    )
    if row is None:
        raise BizError(code=404, message="function not found")
    return dict(row)


@router.delete("/functions/{func_id}")
async def delete_function(func_id: int, pool: DatabasePool = Depends(get_db_pool),
                          user: dict = Depends(require_login)) -> dict:
    if "engineer" not in (user.get("roles") or []) and "admin" not in (user.get("roles") or []):
        raise BizError(code=403, message="engineer+ role required")
    await pool.execute("DELETE FROM running_equipment_function WHERE id = $1", func_id)
    return {"ok": True}
