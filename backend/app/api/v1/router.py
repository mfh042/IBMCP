from fastapi import APIRouter
import time

from app.api.v1 import (
    auth,
    collector_admin,
    control,
    running_config,
    static_dict,
    data,
    devices,
    alarms,
    ws,
)

api_v1_router = APIRouter()


@api_v1_router.get("/ping", tags=["system"])
async def ping() -> dict:
    """前端连通性探测（无鉴权、无数据库依赖）。"""
    return {"status": "ok", "time": int(time.time())}


api_v1_router.include_router(auth.router, prefix="/auth", tags=["auth"])
api_v1_router.include_router(static_dict.router, prefix="/static", tags=["static"])
api_v1_router.include_router(running_config.router, prefix="/config", tags=["config"])
api_v1_router.include_router(data.router, prefix="/data", tags=["data"])
api_v1_router.include_router(devices.router, prefix="/runtime", tags=["runtime"])
api_v1_router.include_router(alarms.router, prefix="/alarms", tags=["alarms"])
api_v1_router.include_router(control.router, prefix="/control", tags=["control"])
api_v1_router.include_router(collector_admin.router, prefix="/collector", tags=["collector"])
api_v1_router.include_router(ws.router, tags=["ws"])
