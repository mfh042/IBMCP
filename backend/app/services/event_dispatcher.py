from __future__ import annotations

import asyncio
import logging
import time
from typing import Any

from app.core.collector.protocol import CollectorApi
from app.db.pool import DatabasePool
from app.services.command_waiter import command_key, command_waiter
from app.ws.hub import WsHub

logger = logging.getLogger(__name__)


class EventDispatcher:
    """扩展事件消费（01 号 §7.3 / 03 号 §3.7）。

    红线：回调在 C++ 采集线程上执行，只做 `call_soon_threadsafe` 投递，
    绝不在回调内做 DB / HTTP / 加锁等待。
    """

    def __init__(self, pool: DatabasePool, collector: CollectorApi, ws_hub: WsHub) -> None:
        self.pool = pool
        self.collector = collector
        self.ws_hub = ws_hub
        self._loop: asyncio.AbstractEventLoop | None = None
        self._queue: asyncio.Queue[dict[str, Any]] | None = None
        self._task: asyncio.Task | None = None
        self._running = False

    def start(self) -> None:
        if self._running:
            return
        self._loop = asyncio.get_event_loop()
        self._queue = asyncio.Queue(maxsize=10000)
        self._running = True
        self._task = self._loop.create_task(self._run())
        self.collector.set_event_callback(self._on_event)
        logger.info("EventDispatcher started")

    def stop(self) -> None:
        self._running = False
        try:
            self.collector.set_event_callback(None)
        except Exception:
            logger.exception("detach event callback failed")
        if self._task is not None:
            self._task.cancel()
            self._task = None
        logger.info("EventDispatcher stopped")

    def _on_event(self, event: dict[str, Any]) -> None:
        loop = self._loop
        queue = self._queue
        if loop is None or queue is None or not self._running:
            return
        try:
            loop.call_soon_threadsafe(self._enqueue, event)
        except RuntimeError:
            # 事件循环已关闭（进程关停阶段），直接丢弃
            pass

    def _enqueue(self, event: dict[str, Any]) -> None:
        if self._queue is None:
            return
        try:
            self._queue.put_nowait(event)
        except asyncio.QueueFull:
            logger.warning("event queue full, dropped event type=%s", event.get("type"))

    async def _run(self) -> None:
        assert self._queue is not None
        while self._running:
            try:
                event = await self._queue.get()
                await self._handle(event)
            except asyncio.CancelledError:
                break
            except Exception as exc:
                logger.exception("event dispatch failed", exc_info=exc)

    async def _handle(self, event: dict[str, Any]) -> None:
        event_type = event.get("type")
        if event_type == "device_status":
            await self._on_device_status(event)
        elif event_type == "write_result":
            await self._on_write_result(event)
        elif event_type == "plan_ack":
            await self._on_plan_ack(event)
        elif event_type == "collector_stats":
            logger.debug("collector_stats %s", event)
        else:
            logger.debug("unknown collector event type=%s", event_type)

    async def _on_device_status(self, event: dict[str, Any]) -> None:
        device_key = str(event.get("device_key") or "")
        if not device_key:
            return
        status = int(event.get("status", 0))
        reason = event.get("reason") or ""
        ts = _event_seconds(event)

        await self.pool.execute(
            "INSERT INTO using_devices (device_code, device_name, status, updated_at) "
            "VALUES ($1, $1, $2, $3) "
            "ON CONFLICT (device_code) DO UPDATE SET "
            " status = EXCLUDED.status, updated_at = EXCLUDED.updated_at",
            device_key,
            status,
            ts,
        )
        await self.pool.execute(
            "INSERT INTO using_device_runtime "
            "(device_key, status, last_error, snapshot_at) "
            "VALUES ($1, $2, $3, $4) "
            "ON CONFLICT (device_key) DO UPDATE SET "
            " status = EXCLUDED.status, "
            " last_error = EXCLUDED.last_error, "
            " snapshot_at = EXCLUDED.snapshot_at",
            device_key,
            status,
            reason or None,
            ts,
        )

        await self.ws_hub.broadcast("device", {
            "type": "device_status",
            "device_code": device_key,
            "status": status,
            "reason": reason,
            "ts": ts,
        })

    async def _on_write_result(self, event: dict[str, Any]) -> None:
        command_id = event.get("command_id")
        if command_id is None:
            return
        success = bool(event.get("success"))
        status = 2 if success else 3
        message = str(event.get("message") or ("" if success else "write failed"))
        ts = _event_seconds(event)

        await self.pool.execute(
            "UPDATE control_commands SET status = $2, message = $3, finished_at = $4 "
            "WHERE ext_command_id = $1",
            int(command_id),
            status,
            message,
            ts,
        )

        await self.ws_hub.broadcast("control", {
            "type": "control_finished",
            "command_id": int(command_id),
            "device_code": event.get("device_key"),
            "point_id": event.get("point_id"),
            "success": success,
            "status": status,
            "message": message,
            "ts": ts,
        })

        device_key = event.get("device_key")
        point_id = event.get("point_id")
        if device_key is not None and point_id is not None:
            command_waiter.resolve(
                command_key(device_key, point_id),
                {
                    "command_id": int(command_id),
                    "success": success,
                    "status": status,
                    "message": message,
                    "elapsed_ms": int(event.get("elapsed_ms") or 0),
                },
            )

    async def _on_plan_ack(self, event: dict[str, Any]) -> None:
        revision = event.get("revision")
        if revision is None:
            return
        raw_status = event.get("status", 0)
        ok = raw_status in (0, "ok", "success", True)
        status = 1 if ok else 2
        error = event.get("error") or None
        ts = _event_seconds(event)

        await self.pool.execute(
            "UPDATE runtime_plan_revision SET status = $2, extension_error = $3, applied_at = $4 "
            "WHERE revision = $1",
            int(revision),
            status,
            error,
            ts,
        )
        if not ok:
            logger.error("plan_ack failed revision=%s error=%s", revision, error)


def _event_seconds(event: dict[str, Any]) -> int:
    ts_ms = event.get("ts_ms")
    if ts_ms is not None:
        return int(ts_ms) // 1000
    for key in ("finished_at", "ts"):
        value = event.get(key)
        if value is not None:
            return int(value)
    return int(time.time())
