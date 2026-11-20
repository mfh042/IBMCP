from __future__ import annotations

import asyncio
import logging
import time

from app.core.collector.protocol import CollectorApi
from app.db.pool import DatabasePool
from app.ws.hub import WsHub

logger = logging.getLogger(__name__)


class ActiveAlarmTracker:
    """内存态活动报警跟踪（每进程一份）。

    key = (device_code, point_address) → level/state/triggered_at/value
    启动时从 DB 预热 state=1 的报警。
    """

    def __init__(self) -> None:
        self.active: dict[tuple[str, str], dict] = {}

    def check_and_decide(
            self,
            device_code: str,
            point_address: str,
            point_name: str,
            value: float,
            quality: str,
            alarm_high: float,
            alarm_low: float,
            alarm_enabled: bool,
            now: int,
    ) -> dict | None:
        if not alarm_enabled or quality != "Good":
            return None
        key = (device_code, point_address)
        curr = self.active.get(key)

        triggered: dict | None = None

        if value > alarm_high or value < alarm_low:
            level = 2
            span = alarm_high - alarm_low if alarm_high > alarm_low else 0
            if span > 0 and abs(value - (alarm_high if value > alarm_high else alarm_low)) > span * 0.5:
                level = 3
            if curr is None or curr.get("state") != 1:
                triggered = {
                    "device_code": device_code,
                    "point_address": point_address,
                    "point_name": point_name,
                    "level": level,
                    "state": 1,
                    "value": value,
                    "triggered_at": now,
                }
                self.active[key] = triggered
            elif level != curr.get("level"):
                self.active[key]["level"] = level
                self.active[key]["value"] = value
                triggered = self.active[key]
        else:
            if curr is not None and curr.get("state") == 1:
                self.active.pop(key, None)
                return {
                    "device_code": device_code,
                    "point_address": point_address,
                    "point_name": point_name,
                    "state": 3,
                    "recovered_at": now,
                }

        return triggered


class SamplePump:
    """采样泵：驱动 snapshot_since → 批量落库 → 报警判定 → WS 广播。

    默认每 200ms 调一次 collector.snapshot_since()，用 COPY 攒批写入 time_series_data。
    """

    def __init__(
            self,
            pool: DatabasePool,
            collector: CollectorApi,
            ws_hub: WsHub,
            flush_interval_ms: int = 200,
    ) -> None:
        self.pool = pool
        self.collector = collector
        self.ws_hub = ws_hub
        self.flush_interval_ms = flush_interval_ms
        self._seq: int = 0
        self._task: asyncio.Task | None = None
        self._running = False
        self._alarm_tracker = ActiveAlarmTracker()
        self._point_cache: dict[str, dict] = {}

    async def _warmup_point_cache(self) -> None:
        rows = await self.pool.fetch(
            "SELECT id, device_code, item_name, address, alarm_high, alarm_low, alarm_enabled, "
            "scale, \"offset\" FROM using_points",
        )
        for r in rows:
            self._point_cache[str(r["id"])] = dict(r)
        logger.info("point cache warmed", count=len(self._point_cache))

    def start(self) -> None:
        if self._running:
            return
        self._running = True
        loop = asyncio.get_event_loop()
        self._task = loop.create_task(self._run())
        logger.info("SamplePump started", flush_interval_ms=self.flush_interval_ms)

    def stop(self) -> None:
        self._running = False
        if self._task is not None:
            self._task.cancel()
            self._task = None
        logger.info("SamplePump stopped")

    async def _run(self) -> None:
        await self._warmup_point_cache()
        while self._running:
            try:
                await self._tick()
            except asyncio.CancelledError:
                break
            except Exception as exc:
                logger.exception("SamplePump._tick error", exc_info=exc)
            await asyncio.sleep(self.flush_interval_ms / 1000.0)

    async def _tick(self) -> None:
        new_seq, samples = self.collector.snapshot_since(self._seq)
        if not samples:
            self._seq = new_seq
            return
        self._seq = new_seq
        now = int(time.time())

        ts_rows, alive_point_ids = self._prepare_ts_rows(samples)
        if ts_rows:
            await self._batch_copy_ts(ts_rows)

        alarm_events = self._evaluate_alarms(samples, now)
        if alarm_events:
            await self._persist_alarms(alarm_events, now)
            await self.ws_hub.broadcast("alarm", {"type": "alarm", "events": alarm_events})

        await self.ws_hub.broadcast("data", {
            "type": "data",
            "ts": now,
            "count": len(samples),
            "samples": [
                {
                    "device_code": s["device_key"],
                    "point_id": s["point_id"],
                    "value": s["value"],
                    "quality": s.get("quality", "Good"),
                }
                for s in samples
            ],
        })

    def _prepare_ts_rows(self, samples: list[dict]) -> tuple[list[tuple], set[str]]:
        rows: list[tuple] = []
        seen: set[tuple] = set()
        alive: set[str] = set()
        for s in samples:
            ts = s.get("ts") or time.time()
            if isinstance(ts, int):
                from datetime import datetime, timezone
                dt = datetime.fromtimestamp(ts / 1000.0, tz=timezone.utc)
            else:
                dt = ts
            pid = str(s["point_id"])
            alive.add(pid)
            key = (dt, pid)
            if key in seen:
                continue
            seen.add(key)
            rows.append((
                dt,
                int(pid),
                float(s["value"]),
                str(s.get("quality", "Good")),
                str(s.get("raw_value", "")),
            ))
        return rows, alive

    async def _batch_copy_ts(self, rows: list[tuple]) -> None:
        conn = await self.pool.pool().acquire()
        try:
            await conn.copy_records_to_table(
                "time_series_data",
                records=rows,
                columns=["ts", "point_id", "value", "quality", "raw_value"],
            )
        except Exception as exc:
            logger.error("COPY time_series_data failed, fallback executemany", exc_info=exc)
            await self.pool.executemany(
                "INSERT INTO time_series_data (ts, point_id, value, quality, raw_value) "
                "VALUES ($1, $2, $3, $4, $5) ON CONFLICT DO NOTHING",
                [(r[0], r[1], r[2], r[3], r[4]) for r in rows],
            )
        finally:
            await self.pool.pool().release(conn)

    def _evaluate_alarms(self, samples: list[dict], now: int) -> list[dict]:
        events: list[dict] = []
        for s in samples:
            pid = str(s["point_id"])
            meta = self._point_cache.get(pid)
            if meta is None:
                continue
            quality = str(s.get("quality", "Good"))
            value = float(s["value"])
            triggered = self._alarm_tracker.check_and_decide(
                device_code=meta["device_code"],
                point_address=meta["address"],
                point_name=meta.get("item_name") or "",
                value=value,
                quality=quality,
                alarm_high=float(meta.get("alarm_high") or 0),
                alarm_low=float(meta.get("alarm_low") or 0),
                alarm_enabled=bool(meta.get("alarm_enabled")),
                now=now,
            )
            if triggered:
                events.append(triggered)
        return events

    async def _persist_alarms(self, events: list[dict], now: int) -> None:
        for ev in events:
            if ev.get("state") == 3:
                await self.pool.execute(
                    "UPDATE using_alarms SET state = 3, recovered_at = $1 "
                    "WHERE device_code = $2 AND point_address = $3 AND state = 1",
                    ev["recovered_at"], ev["device_code"], ev["point_address"],
                )
            else:
                await self.pool.execute(
                    "INSERT INTO using_alarms (device_code, point_address, point_name, level, state, message, value, triggered_at) "
                    "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) "
                    "ON CONFLICT DO NOTHING",
                    ev["device_code"], ev["point_address"], ev.get("point_name") or "",
                    ev["level"], 1, "", ev["value"], ev["triggered_at"],
                )
