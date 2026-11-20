from __future__ import annotations

from fastapi import APIRouter, Depends

from app.core.collector.protocol import CollectorApi
from app.core.deps import get_collector_backend, get_db_pool, require_login
from app.core.errors import BizError
from app.db.pool import DatabasePool
from app.services.control_service import ControlService

router = APIRouter()


@router.post("/write")
async def write_point(
        body: dict,
        pool: DatabasePool = Depends(get_db_pool),
        collector: CollectorApi = Depends(get_collector_backend),
        user: dict = Depends(require_login),
) -> dict:
    roles = user.get("roles") or []
    if "engineer" not in roles and "admin" not in roles and "operator" not in roles:
        raise BizError(code=403, message="operator+ role required")
    device_code = body.get("device_code") or body.get("device_key")
    point_id = body.get("point_id")
    value = body.get("value")
    if not device_code or point_id is None or value is None:
        raise BizError(code=422, message="device_code, point_id, value required")
    svc = ControlService(pool, collector)
    return await svc.write(device_code, int(point_id), str(value), point_name=body.get("point_name"))


@router.get("/commands/{command_id}")
async def get_command(
        command_id: int,
        pool: DatabasePool = Depends(get_db_pool),
        collector: CollectorApi = Depends(get_collector_backend),
        user: dict = Depends(require_login),
) -> dict:
    svc = ControlService(pool, collector)
    return await svc.get_command(command_id)


@router.get("/queue-depth")
async def queue_depth(
        pool: DatabasePool = Depends(get_db_pool),
        collector: CollectorApi = Depends(get_collector_backend),
        user: dict = Depends(require_login),
) -> dict:
    svc = ControlService(pool, collector)
    return await svc.queue_depth()
