from __future__ import annotations

import logging
from datetime import datetime, timezone
from typing import Any

from app.db.pool import DatabasePool

logger = logging.getLogger(__name__)

_BUCKET_SECONDS = {
    "1min": 60,
    "5min": 300,
    "1h": 3600,
    "1d": 86400,
}

_BUCKET_TRUNC = {
    "1min": "date_trunc('minute', ts)",
    "5min": "date_trunc('hour', ts) + floor(extract(minute from ts) / 5) * interval '5 minutes'",
    "1h": "date_trunc('hour', ts)",
    "1d": "date_trunc('day', ts)",
}

_AGG_FUNC_MAP = {
    "avg": "AVG",
    "min": "MIN",
    "max": "MAX",
    "sum": "SUM",
    "count": "COUNT",
}


def _epoch_to_ts(epoch_seconds: int) -> str:
    return datetime.fromtimestamp(epoch_seconds, tz=timezone.utc).isoformat()


def _ts_to_epoch(ts) -> int:
    if isinstance(ts, datetime):
        return int(ts.timestamp())
    return int(ts)


class QueryService:
    def __init__(self, pool: DatabasePool) -> None:
        self.pool = pool

    async def history(
            self,
            point_id: int,
            start_ts: int,
            end_ts: int,
            bucket: str = "raw",
            func: str = "avg",
            limit: int = 50000,
    ) -> dict[str, Any]:
        start_iso = _epoch_to_ts(start_ts)
        end_iso = _epoch_to_ts(end_ts)

        if bucket == "raw":
            rows = await self.pool.fetch(
                "SELECT ts, point_id, value, quality, raw_value "
                "FROM time_series_data "
                "WHERE point_id = $1 AND ts >= $2 AND ts <= $3 "
                "ORDER BY ts ASC LIMIT $4",
                point_id, start_iso, end_iso, limit,
            )
            items = [
                {
                    "point_id": r["point_id"],
                    "value": float(r["value"]),
                    "quality": r["quality"],
                    "ts": _ts_to_epoch(r["ts"]),
                    "raw_value": r["raw_value"],
                }
                for r in rows
            ]
        else:
            bucket_trunc = _BUCKET_TRUNC.get(bucket)
            if bucket_trunc is None:
                raise ValueError(f"unknown bucket: {bucket}")
            agg_expr = _AGG_FUNC_MAP.get(func, "AVG")
            rows = await self.pool.fetch(
                f"SELECT {bucket_trunc} AS bucket, "
                f"{agg_expr}(value) AS agg_value, "
                "COUNT(*) AS sample_count, "
                "(array_agg(quality ORDER BY ts DESC))[1] AS last_quality "
                "FROM time_series_data "
                "WHERE point_id = $1 AND ts >= $2 AND ts <= $3 "
                "GROUP BY bucket ORDER BY bucket ASC LIMIT $4",
                point_id, start_iso, end_iso, limit,
            )
            items = [
                {
                    "point_id": point_id,
                    "value": float(r["agg_value"]) if r["agg_value"] is not None else None,
                    "ts": _ts_to_epoch(r["bucket"]),
                    "sample_count": int(r["sample_count"]),
                    "last_quality": r["last_quality"],
                }
                for r in rows
            ]

        return {
            "point_id": point_id,
            "start_ts": start_ts,
            "end_ts": end_ts,
            "bucket": bucket,
            "func": func,
            "items": items,
            "total": len(items),
        }

    async def latest(
            self,
            device_code: str | None = None,
            point_ids: str | None = None,
    ) -> dict[str, Any]:
        point_id_list: list[int] | None = None
        if point_ids:
            point_id_list = [int(p.strip()) for p in point_ids.split(",") if p.strip()]

        if device_code:
            if point_id_list:
                rows = await self.pool.fetch(
                    "SELECT DISTINCT ON (t.point_id) "
                    "p.device_code, t.point_id, t.value, t.quality, t.ts, p.item_name "
                    "FROM time_series_data t "
                    "JOIN using_points p ON p.id = t.point_id "
                    "WHERE p.device_code = $1 AND t.point_id = ANY($2::int[]) "
                    "ORDER BY t.point_id, t.ts DESC",
                    device_code, point_id_list,
                )
            else:
                rows = await self.pool.fetch(
                    "SELECT DISTINCT ON (t.point_id) "
                    "p.device_code, t.point_id, t.value, t.quality, t.ts, p.item_name "
                    "FROM time_series_data t "
                    "JOIN using_points p ON p.id = t.point_id "
                    "WHERE p.device_code = $1 "
                    "ORDER BY t.point_id, t.ts DESC",
                    device_code,
                )
        elif point_id_list:
            rows = await self.pool.fetch(
                "SELECT DISTINCT ON (t.point_id) "
                "p.device_code, t.point_id, t.value, t.quality, t.ts, p.item_name "
                "FROM time_series_data t "
                "JOIN using_points p ON p.id = t.point_id "
                "WHERE t.point_id = ANY($1::int[]) "
                "ORDER BY t.point_id, t.ts DESC",
                point_id_list,
            )
        else:
            rows = await self.pool.fetch(
                "SELECT DISTINCT ON (p.device_code, t.point_id) "
                "p.device_code, t.point_id, t.value, t.quality, t.ts, p.item_name "
                "FROM time_series_data t "
                "JOIN using_points p ON p.id = t.point_id "
                "ORDER BY p.device_code, t.point_id, t.ts DESC",
            )

        items = [
            {
                "device_code": r["device_code"],
                "point_id": r["point_id"],
                "item_name": r["item_name"],
                "value": float(r["value"]),
                "quality": r["quality"],
                "ts": _ts_to_epoch(r["ts"]),
            }
            for r in rows
        ]
        return {
            "device_code": device_code,
            "point_ids": point_id_list,
            "items": items,
            "total": len(items),
        }
