from __future__ import annotations

import asyncio
import logging
from typing import Any, Literal

from fastapi import APIRouter, Depends, Query

from app.core.deps import get_db_pool, require_login
from app.core.errors import BizError
from app.db.pool import DatabasePool

logger = logging.getLogger(__name__)
router = APIRouter()

RESOURCE_SPECS: dict[str, dict[str, str]] = {
    "devices": {"table": "base_device_base_info", "code": "device_code", "name": "device_name"},
    "device_categories": {"table": "base_device_category_dict", "code": "category_code", "name": "category_name"},
    "brands": {"table": "base_device_brand_model_dict", "code": "brand_code", "name": "brand_name"},
    "ports": {"table": "base_device_port_dict", "code": "port_code", "name": "port_name"},
    "protocols": {"table": "base_device_protocol_dict", "code": "protocol_code", "name": "protocol_name"},
    "data_items": {"table": "base_device_data_item_dict", "code": "item_code", "name": "item_name"},
    "equipments": {"table": "base_equipment_base_info", "code": "equipment_code", "name": "equipment_name"},
    "equipment_functions": {"table": "base_equipment_function_dict", "code": "function_code", "name": "function_name"},
    "process_routes": {"table": "base_process_route_resource", "code": "route_code", "name": "route_name"},
    "process_steps": {"table": "base_process_step", "code": "step_code", "name": "step_name"},
}

ResourceName = Literal[
    "devices", "device_categories", "brands", "ports", "protocols",
    "data_items", "equipments", "equipment_functions", "process_routes", "process_steps",
]


def _spec(resource: str) -> dict[str, str]:
    if resource not in RESOURCE_SPECS:
        raise BizError(code=400, message=f"unknown static resource: {resource}")
    return RESOURCE_SPECS[resource]


def _check_engineer(user: dict) -> None:
    roles = user.get("roles") or []
    if "engineer" not in roles and "admin" not in roles:
        raise BizError(code=403, message="engineer+ role required for write")


@router.get("/{resource}")
async def list_dict(
        resource: ResourceName,
        page: int = Query(1, ge=1),
        size: int = Query(20, ge=1, le=200),
        keyword: str | None = None,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    spec = _spec(resource)
    table = spec["table"]
    name_col = spec["name"]
    code_col = spec["code"]
    where = ""
    args: list[Any] = []
    if keyword:
        where = f"WHERE {code_col} ILIKE $1 OR {name_col} ILIKE $1"
        args.append(f"%{keyword}%")
    rows = await pool.fetch(
        f"SELECT * FROM {table} {where} ORDER BY id "
        f"LIMIT ${len(args) + 1} OFFSET ${len(args) + 2}",
        *args, size, (page - 1) * size,
    )
    total_row = await pool.fetchrow(f"SELECT COUNT(*) AS cnt FROM {table} {where}", *args)
    return {"items": [dict(r) for r in rows], "total": int(total_row["cnt"])}


@router.get("/{resource}/all")
async def all_dict(
        resource: ResourceName,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    spec = _spec(resource)
    rows = await pool.fetch(f"SELECT * FROM {spec['table']} ORDER BY id")
    return {"items": [dict(r) for r in rows]}


@router.get("/{resource}/{row_id}")
async def get_dict(
        resource: ResourceName,
        row_id: int,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    spec = _spec(resource)
    row = await pool.fetchrow(f"SELECT * FROM {spec['table']} WHERE id = $1", row_id)
    if row is None:
        raise BizError(code=404, message=f"{resource} row not found")
    return dict(row)


@router.post("/{resource}")
async def create_dict(
        resource: ResourceName,
        body: dict,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    _check_engineer(user)
    spec = _spec(resource)
    table = spec["table"]
    now = int(__import__("time").time())
    data = {**body, "created_at": now, "updated_at": now}
    cols = ", ".join(data.keys())
    ph = ", ".join(f"${i + 1}" for i in range(len(data)))
    try:
        row = await pool.fetchrow(f"INSERT INTO {table} ({cols}) VALUES ({ph}) RETURNING *", *data.values())
    except Exception as exc:
        msg = str(exc).lower()
        if "unique" in msg or "duplicate" in msg:
            raise BizError(code=2001, message="code already exists") from exc
        raise BizError(code=500, message=f"insert failed: {exc}") from exc
    return dict(row)


@router.put("/{resource}/{row_id}")
async def update_dict(
        resource: ResourceName,
        row_id: int,
        body: dict,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    _check_engineer(user)
    spec = _spec(resource)
    table = spec["table"]
    now = int(__import__("time").time())
    data = {**body, "updated_at": now}
    sets = ", ".join(f"{k} = ${i + 1}" for i, k in enumerate(data.keys()))
    vals = list(data.values()) + [row_id]
    try:
        row = await pool.fetchrow(
            f"UPDATE {table} SET {sets} WHERE id = ${len(vals)} RETURNING *", *vals,
        )
    except Exception as exc:
        raise BizError(code=500, message=f"update failed: {exc}") from exc
    if row is None:
        raise BizError(code=404, message=f"{resource} row not found")
    return dict(row)


@router.delete("/{resource}/{row_id}")
async def delete_dict(
        resource: ResourceName,
        row_id: int,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    _check_engineer(user)
    spec = _spec(resource)
    table = spec["table"]
    try:
        result = await pool.execute(f"DELETE FROM {table} WHERE id = $1", row_id)
    except Exception as exc:
        msg = str(exc).lower()
        if "foreign" in msg or "referenced" in msg:
            raise BizError(code=2002, message="referenced by running data, cannot delete") from exc
        raise BizError(code=500, message=f"delete failed: {exc}") from exc
    return {"ok": "DELETE 1" in result}


@router.get("/bootstrap")
async def bootstrap(pool: DatabasePool = Depends(get_db_pool), user: dict = Depends(require_login)) -> dict:
    results = await asyncio.gather(*[
        pool.fetch(f"SELECT * FROM {spec['table']} ORDER BY id")
        for spec in RESOURCE_SPECS.values()
    ])
    return {resource: [dict(r) for r in results[i]] for i, resource in enumerate(RESOURCE_SPECS)}
