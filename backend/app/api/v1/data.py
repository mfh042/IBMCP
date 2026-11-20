from __future__ import annotations

import logging
from typing import Literal

from fastapi import APIRouter, Depends, Query

from app.core.deps import get_db_pool, require_login
from app.core.errors import BizError
from app.db.pool import DatabasePool
from app.services.query_service import QueryService

logger = logging.getLogger(__name__)
router = APIRouter()

AggBucket = Literal["raw", "1min", "5min", "1h", "1d"]
AggFunc = Literal["avg", "min", "max", "sum", "count"]


@router.get("/history")
async def history(
        point_id: int,
        start_ts: int,
        end_ts: int,
        bucket: AggBucket = Query("raw"),
        func: AggFunc = Query("avg"),
        limit: int = Query(50000, ge=1, le=200000),
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    if end_ts <= start_ts:
        raise BizError(code=400, message="end_ts must be greater than start_ts")
    svc = QueryService(pool)
    return await svc.history(point_id, start_ts, end_ts, bucket, func, limit)


@router.get("/latest")
async def latest(
        device_code: str | None = None,
        point_ids: str | None = None,
        pool: DatabasePool = Depends(get_db_pool),
        user: dict = Depends(require_login),
) -> dict:
    svc = QueryService(pool)
    return await svc.latest(device_code, point_ids)
