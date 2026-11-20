from __future__ import annotations

from fastapi import APIRouter, Depends

from app.core.collector.protocol import CollectorApi
from app.core.deps import get_collector_backend, require_login
from app.core.errors import BizError

router = APIRouter()


@router.get("/health")
async def health(
        collector: CollectorApi = Depends(get_collector_backend),
        user: dict = Depends(require_login),
) -> dict:
    return collector.health()


@router.get("/protocols")
async def list_protocols(
        collector: CollectorApi = Depends(get_collector_backend),
        user: dict = Depends(require_login),
) -> dict:
    return {"protocols": collector.list_protocols()}


@router.get("/stats")
async def stats(
        collector: CollectorApi = Depends(get_collector_backend),
        user: dict = Depends(require_login),
) -> dict:
    h = collector.health()
    return {
        "revision": h.get("revision", 0),
        "device_count": h.get("device_count", 0),
        "point_count": h.get("point_count", 0),
        "queue_depth": h.get("queue_depth", 0),
        "uptime_sec": h.get("uptime_sec", 0),
    }


@router.post("/reload")
async def reload(
        collector: CollectorApi = Depends(get_collector_backend),
        user: dict = Depends(require_login),
) -> dict:
    roles = user.get("roles") or []
    if "admin" not in roles:
        raise BizError(code=403, message="admin required")
    collector.shutdown()
    collector.init()
    return {"ok": True, "message": "collector reloaded"}
