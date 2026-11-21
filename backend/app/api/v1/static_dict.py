from __future__ import annotations

import asyncio
import json
import logging
import time
from typing import Any, Literal

from fastapi import APIRouter, Depends, Query, Request

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
    "process_steps": {"table": "base_process_route_step", "code": "step_code", "name": "step_name"},
}

ResourceName = Literal[
    "devices", "device_categories", "brands", "ports", "protocols",
    "data_items", "equipments", "equipment_functions", "process_routes", "process_steps",
]

# 只读列，禁止客户端写入
_READONLY_COLUMNS = {"id", "created_at", "updated_at"}

# 删除前的引用检查（02 号 §3.11）
_REFERENCE_SPECS: dict[str, list[tuple[str, str]]] = {
    "devices": [
        ("running_route_device", "device_code"),
        ("base_process_route_step", "device_code"),
    ],
    "device_categories": [
        ("base_device_base_info", "category_code"),
        ("base_device_brand_model_dict", "category_code"),
        ("base_device_data_item_dict", "category_code"),
        ("base_equipment_base_info", "category_code"),
        ("base_equipment_function_dict", "category_code"),
    ],
    "brands": [
        ("base_device_base_info", "brand_code"),
        ("base_equipment_base_info", "brand_code"),
    ],
    "ports": [("base_device_base_info", "port_code")],
    "protocols": [("base_device_data_item_dict", "protocol_code")],
    "data_items": [("running_device_collection", "item_code")],
    "equipments": [("running_equipment_config", "equipment_code")],
    "equipment_functions": [("running_equipment_function", "function_code")],
    "process_routes": [
        ("running_equipment_route", "route_code"),
        ("base_process_route_step", "route_code"),
    ],
    "process_steps": [],  # 联合键特判，见 _check_references
}

_FUNCTION_TYPES = {"采集", "控制", "报警", "追溯"}
_READ_WRITE = {"R", "W", "RW"}

_COLUMN_CACHE: dict[str, dict[str, dict]] = {}
_COLUMN_LOCK = asyncio.Lock()


def _spec(resource: str) -> dict[str, str]:
    if resource not in RESOURCE_SPECS:
        raise BizError(code=400, message=f"unknown static resource: {resource}")
    return RESOURCE_SPECS[resource]


def _check_engineer(user: dict) -> None:
    roles = user.get("roles") or []
    if "engineer" not in roles and "admin" not in roles:
        raise BizError(code=403, message="engineer+ role required for write")


async def _columns(pool: DatabasePool, table: str) -> dict[str, dict]:
    """读取并缓存列元数据（列名 / 类型 / 可空 / 长度 / 默认值）。"""
    if table in _COLUMN_CACHE:
        return _COLUMN_CACHE[table]
    async with _COLUMN_LOCK:
        if table in _COLUMN_CACHE:
            return _COLUMN_CACHE[table]
        rows = await pool.fetch(
            "SELECT column_name, data_type, is_nullable, character_maximum_length, column_default "
            "FROM information_schema.columns "
            "WHERE table_schema = 'public' AND table_name = $1",
            table,
        )
        if not rows:
            raise BizError(code=500, message=f"table metadata missing: {table}")
        _COLUMN_CACHE[table] = {r["column_name"]: dict(r) for r in rows}
        return _COLUMN_CACHE[table]


def _decode_row(row: Any, meta: dict[str, dict]) -> dict:
    data = dict(row)
    for col, value in data.items():
        if isinstance(value, str) and meta.get(col, {}).get("data_type") in ("json", "jsonb"):
            try:
                data[col] = json.loads(value)
            except json.JSONDecodeError:
                pass
    return data


def _coerce(value: Any, col: str, meta: dict) -> Any:
    dt = meta["data_type"]
    if value is None:
        return None
    if dt in ("integer", "bigint", "smallint"):
        try:
            return int(value)
        except (TypeError, ValueError):
            raise BizError(code=422, message=f"field {col} must be an integer")
    if dt in ("double precision", "real", "numeric"):
        try:
            return float(value)
        except (TypeError, ValueError):
            raise BizError(code=422, message=f"field {col} must be a number")
    if dt == "boolean":
        if isinstance(value, bool):
            return value
        if isinstance(value, (int, str)) and str(value).lower() in ("1", "0", "true", "false"):
            return str(value).lower() in ("1", "true")
        raise BizError(code=422, message=f"field {col} must be a boolean")
    if dt in ("json", "jsonb"):
        if isinstance(value, (dict, list)):
            return json.dumps(value, ensure_ascii=False)
        if isinstance(value, str):
            try:
                json.loads(value)
            except json.JSONDecodeError:
                raise BizError(code=422, message=f"field {col} must be valid JSON")
            return value
        raise BizError(code=422, message=f"field {col} must be a JSON object/array")
    if dt.startswith("character") or dt == "text":
        text = str(value)
        limit = meta.get("character_maximum_length")
        if limit and len(text) > int(limit):
            raise BizError(
                code=422,
                message=f"field {col} exceeds max length {limit}",
                data={"field": col, "length": len(text), "max_length": int(limit)},
            )
        return text
    return value


def _validate_body(resource: str, body: dict, meta: dict[str, dict], *, partial: bool) -> dict:
    if not isinstance(body, dict):
        raise BizError(code=422, message="request body must be an object")

    unknown = [k for k in body if k not in meta]
    if unknown:
        raise BizError(
            code=422,
            message=f"unknown field(s) for {resource}",
            data={"fields": unknown},
        )

    writable = [k for k in body if k not in _READONLY_COLUMNS]
    readonly = [k for k in body if k in _READONLY_COLUMNS]
    if readonly:
        raise BizError(code=422, message="read-only field(s) not allowed", data={"fields": readonly})
    if not writable:
        raise BizError(code=422, message="empty request body")

    data = {}
    for col in writable:
        data[col] = _coerce(body[col], col, meta[col])

    if not partial:
        missing = [
            col for col, m in meta.items()
            if col not in _READONLY_COLUMNS
            and m["is_nullable"] == "NO"
            and m.get("column_default") is None
            and col not in data
        ]
        if missing:
            raise BizError(code=422, message="missing required field(s)", data={"fields": missing})
    return data


async def _cross_check(pool: DatabasePool, resource: str, data: dict) -> None:
    """跨表一致性校验（02 号 §3.11）。"""

    async def exists(table: str, col: str, value: Any, label: str) -> None:
        row = await pool.fetchrow(f"SELECT 1 FROM {table} WHERE {col} = $1 LIMIT 1", value)
        if row is None:
            raise BizError(code=400, message=f"{label} not found: {value}")

    if data.get("category_code") and resource in {
        "devices", "equipments", "equipment_functions", "data_items", "brands",
    }:
        await exists("base_device_category_dict", "category_code", data["category_code"], "category_code")

    if data.get("protocol_code") and resource in {"data_items", "devices"}:
        await exists("base_device_protocol_dict", "protocol_code", data["protocol_code"], "protocol_code")

    if data.get("port_code") and resource == "devices":
        await exists("base_device_port_dict", "port_code", data["port_code"], "port_code")

    if data.get("brand_code") and data.get("model_code"):
        row = await pool.fetchrow(
            "SELECT 1 FROM base_device_brand_model_dict WHERE brand_code = $1 AND model_code = $2",
            data["brand_code"], data["model_code"],
        )
        if row is None:
            raise BizError(
                code=400,
                message=f"brand model not found: {data['brand_code']}/{data['model_code']}",
            )

    if data.get("route_code") and resource == "process_steps":
        await exists("base_process_route_resource", "route_code", data["route_code"], "route_code")

    if data.get("function_type") is not None:
        if data["function_type"] not in _FUNCTION_TYPES:
            raise BizError(
                code=422,
                message=f"function_type must be one of {sorted(_FUNCTION_TYPES)}",
            )

    if resource == "data_items":
        if "read_write" in data and data["read_write"] not in _READ_WRITE:
            raise BizError(code=422, message="read_write must be one of R/W/RW")
        if "scale" in data and float(data["scale"]) == 0.0:
            raise BizError(code=422, message="scale must not be 0")
        if "collection_cycle" in data and int(data["collection_cycle"]) < 1:
            raise BizError(code=422, message="collection_cycle must be >= 1 second")

    if "status" in data and int(data["status"]) not in (0, 1):
        raise BizError(code=422, message="status must be 0 or 1")


async def _check_references(pool: DatabasePool, resource: str, row_id: int) -> None:
    spec = _spec(resource)
    row = await pool.fetchrow(f"SELECT * FROM {spec['table']} WHERE id = $1", row_id)
    if row is None:
        raise BizError(code=404, message=f"{resource} row not found")

    refs: list[dict] = []
    if resource == "process_steps":
        cnt = await pool.fetchrow(
            "SELECT COUNT(*) AS cnt FROM running_route_device WHERE route_code = $1 AND step_code = $2",
            row["route_code"], row["step_code"],
        )
        if int(cnt["cnt"]) > 0:
            refs.append({"table": "running_route_device", "count": int(cnt["cnt"])})
    elif resource == "brands":
        cnt = await pool.fetchrow(
            "SELECT COUNT(*) AS cnt FROM base_device_base_info "
            "WHERE brand_code = $1 AND model_code = $2",
            row["brand_code"], row["model_code"],
        )
        if int(cnt["cnt"]) > 0:
            refs.append({"table": "base_device_base_info", "count": int(cnt["cnt"])})
    else:
        for ref_table, ref_col in _REFERENCE_SPECS.get(resource, []):
            cnt = await pool.fetchrow(
                f"SELECT COUNT(*) AS cnt FROM {ref_table} WHERE {ref_col} = $1",
                row[spec["code"]],
            )
            if int(cnt["cnt"]) > 0:
                refs.append({"table": ref_table, "count": int(cnt["cnt"])})

    if refs:
        raise BizError(
            code=409,
            message=f"{resource} is referenced and cannot be deleted",
            data={"references": refs},
        )


@router.get("/bootstrap")
async def bootstrap(pool: DatabasePool = Depends(get_db_pool), user: dict = Depends(require_login)) -> dict:
    """一次拉取全部静态字典（前端启动用）。

    必须注册在 `/{resource}` 之前，否则会被动态段吞掉并返回 422。
    """
    results = await asyncio.gather(*[
        pool.fetch(f"SELECT * FROM {spec['table']} ORDER BY id")
        for spec in RESOURCE_SPECS.values()
    ])
    output: dict[str, list] = {}
    for resource, spec in RESOURCE_SPECS.items():
        meta = await _columns(pool, spec["table"])
        idx = list(RESOURCE_SPECS).index(resource)
        output[resource] = [_decode_row(r, meta) for r in results[idx]]
    return output


@router.get("/{resource}")
async def list_dict(
        resource: ResourceName,
        request: Request,
        page: int = Query(1, ge=1),
        size: int = Query(20, ge=1, le=200),
        keyword: str | None = None,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    spec = _spec(resource)
    meta = await _columns(pool, spec["table"])

    # 额外查询参数按真实列做等值过滤（如 process_steps?route_code=R1）
    reserved = {"page", "size", "keyword"}
    filters = {k: v for k, v in request.query_params.items() if k not in reserved}
    unknown = [k for k in filters if k not in meta]
    if unknown:
        raise BizError(code=422, message="unknown filter field(s)", data={"fields": unknown})

    clauses: list[str] = []
    args: list[Any] = []
    for col, value in filters.items():
        args.append(value)
        clauses.append(f"{col} = ${len(args)}")
    if keyword:
        args.append(f"%{keyword}%")
        clauses.append(f"({spec['code']} ILIKE ${len(args)} OR {spec['name']} ILIKE ${len(args)})")

    where = f"WHERE {' AND '.join(clauses)}" if clauses else ""
    rows = await pool.fetch(
        f"SELECT * FROM {spec['table']} {where} ORDER BY id "
        f"LIMIT ${len(args) + 1} OFFSET ${len(args) + 2}",
        *args, size, (page - 1) * size,
    )
    total_row = await pool.fetchrow(f"SELECT COUNT(*) AS cnt FROM {spec['table']} {where}", *args)
    return {
        "items": [_decode_row(r, meta) for r in rows],
        "total": int(total_row["cnt"]),
    }


@router.get("/{resource}/all")
async def all_dict(
        resource: ResourceName,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    spec = _spec(resource)
    meta = await _columns(pool, spec["table"])
    rows = await pool.fetch(f"SELECT * FROM {spec['table']} ORDER BY id")
    return {"items": [_decode_row(r, meta) for r in rows]}


@router.get("/{resource}/code/{code}")
async def get_dict_by_code(
        resource: ResourceName,
        code: str,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    spec = _spec(resource)
    meta = await _columns(pool, spec["table"])
    row = await pool.fetchrow(
        f"SELECT * FROM {spec['table']} WHERE {spec['code']} = $1 LIMIT 1", code,
    )
    if row is None:
        raise BizError(code=404, message=f"{resource} not found: {code}")
    return _decode_row(row, meta)


@router.get("/{resource}/{row_id}")
async def get_dict(
        resource: ResourceName,
        row_id: int,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    spec = _spec(resource)
    meta = await _columns(pool, spec["table"])
    row = await pool.fetchrow(f"SELECT * FROM {spec['table']} WHERE id = $1", row_id)
    if row is None:
        raise BizError(code=404, message=f"{resource} row not found")
    return _decode_row(row, meta)


@router.post("/{resource}")
async def create_dict(
        resource: ResourceName,
        body: dict,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    _check_engineer(user)
    spec = _spec(resource)
    meta = await _columns(pool, spec["table"])
    data = _validate_body(resource, body, meta, partial=False)
    await _cross_check(pool, resource, data)

    now = int(time.time())
    data["created_at"] = now
    data["updated_at"] = now
    cols = ", ".join(data.keys())
    ph = ", ".join(f"${i + 1}" for i in range(len(data)))
    try:
        row = await pool.fetchrow(
            f"INSERT INTO {spec['table']} ({cols}) VALUES ({ph}) RETURNING *", *data.values(),
        )
    except Exception as exc:
        msg = str(exc).lower()
        if "unique" in msg or "duplicate" in msg:
            raise BizError(code=409, message=f"{spec['code']} already exists") from exc
        if "foreign key" in msg:
            raise BizError(code=400, message="referenced row not found") from exc
        logger.exception("insert %s failed", spec["table"])
        raise BizError(code=500, message="insert failed") from exc
    return _decode_row(row, meta)


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
    meta = await _columns(pool, spec["table"])
    data = _validate_body(resource, body, meta, partial=True)
    await _cross_check(pool, resource, data)

    data["updated_at"] = int(time.time())
    sets = ", ".join(f"{k} = ${i + 1}" for i, k in enumerate(data.keys()))
    vals = list(data.values()) + [row_id]
    try:
        row = await pool.fetchrow(
            f"UPDATE {spec['table']} SET {sets} WHERE id = ${len(vals)} RETURNING *", *vals,
        )
    except Exception as exc:
        msg = str(exc).lower()
        if "unique" in msg or "duplicate" in msg:
            raise BizError(code=409, message=f"{spec['code']} already exists") from exc
        logger.exception("update %s failed", spec["table"])
        raise BizError(code=500, message="update failed") from exc
    if row is None:
        raise BizError(code=404, message=f"{resource} row not found")
    return _decode_row(row, meta)


@router.delete("/{resource}/{row_id}")
async def delete_dict(
        resource: ResourceName,
        row_id: int,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    _check_engineer(user)
    spec = _spec(resource)
    await _check_references(pool, resource, row_id)
    try:
        result = await pool.execute(f"DELETE FROM {spec['table']} WHERE id = $1", row_id)
    except Exception as exc:
        msg = str(exc).lower()
        if "foreign" in msg or "referenced" in msg:
            raise BizError(code=409, message="referenced by running data, cannot delete") from exc
        logger.exception("delete %s failed", spec["table"])
        raise BizError(code=500, message="delete failed") from exc
    return {"ok": "DELETE 1" in result}
