from __future__ import annotations

import asyncio
import logging
import time
from typing import Any

from app.core.collector.ffi import get_collector
from app.core.collector.protocol import CollectorApi
from app.core.errors import BizError
from app.db.pool import DatabasePool
from app.services.command_waiter import command_key, command_waiter

logger = logging.getLogger(__name__)

WAIT_TIMEOUT_S = 3.0


class ControlService:
    """控制命令服务（对齐 03 号 3.7 + 06 号 R4 红线）。

    仅允许对 plan 内设备下发；点位 read_write ∈ {W, RW}；
    设备在线；值类型可解析。
    """

    def __init__(self, pool: DatabasePool, collector: CollectorApi) -> None:
        self.pool = pool
        self.collector = collector

    async def write(
            self,
            device_code: str,
            point_id: int,
            value: str,
            point_name: str | None = None,
            wait: bool = False,
            issued_by: str | None = None,
            timeout: float = WAIT_TIMEOUT_S,
    ) -> dict[str, Any]:
        # point_id 为 plan 点位主键（= running_device_collection.id，02 号 §4.5）
        binding = await self.pool.fetchrow(
            "SELECT rc.id AS binding_id, rc.device_code, rc.protocol_code, "
            "col.id AS collection_id, "
            "COALESCE(col.read_write, it.read_write, 'R') AS read_write, "
            "COALESCE(col.data_type, it.data_type, 'float') AS data_type, "
            "col.item_code, p.id AS using_point_id "
            "FROM running_route_device rc "
            "JOIN running_device_collection col ON col.device_binding_id = rc.id "
            "JOIN base_device_data_item_dict it ON it.item_code = col.item_code "
            "LEFT JOIN using_points p "
            "  ON p.device_code = rc.device_code AND p.using_point_id = col.id "
            "WHERE rc.device_code = $1 AND col.id = $2 "
            "AND rc.enabled = 1 AND col.enabled = 1",
            device_code,
            point_id,
        )
        if binding is None:
            raise BizError(code=404, message=f"device/point not in running config: {device_code}:{point_id}")
        if binding["using_point_id"] is None:
            raise BizError(
                code=404,
                message=f"point {point_id} has no runtime snapshot, activate config first",
                data={"device_code": device_code, "point_id": point_id},
            )

        read_write = (binding["read_write"] or "R").upper()
        if read_write not in ("W", "RW"):
            raise BizError(
                code=4101,
                message=f"point is read-only (read_write={read_write})",
                data={"point_id": point_id},
            )

        runtime = await self.pool.fetchrow(
            "SELECT status FROM using_devices WHERE device_code = $1",
            device_code,
        )
        if runtime is None or int(runtime["status"]) != 1:
            raise BizError(
                code=4103,
                message=f"device {device_code} is offline",
                data={"device_code": device_code},
            )

        self._validate_value(value, binding["data_type"])

        func_row = await self.pool.fetchrow(
            "SELECT 1 FROM running_equipment_function f "
            "JOIN running_equipment_config c ON c.id = f.config_id "
            "JOIN running_equipment_route r ON r.config_id = c.id "
            "JOIN running_route_device rc ON rc.route_config_id = r.id "
            "WHERE rc.device_code = $1 AND f.function_type = '控制' AND f.enabled = 1 "
            "LIMIT 1",
            device_code,
        )
        if func_row is None:
            raise BizError(
                code=4104,
                message=f"no active control function bound to device {device_code}",
                data={"device_code": device_code},
            )

        key = command_key(device_code, point_id)
        fut = command_waiter.register(key) if wait else None

        try:
            cmd_id = self.collector.write_point(device_code, point_id, value)
        except Exception as exc:
            command_waiter.discard(key, fut)
            logger.exception("write_point failed device=%s point=%d", device_code, point_id)
            raise BizError(code=500, message=f"write rejected by collector: {exc}") from exc

        await self.pool.execute(
            "INSERT INTO control_commands "
            "(device_id, point_id, point_name, value, status, issued_at, issued_by, ext_command_id) "
            "SELECT d.id, $2, $3, $4, 0, $5, $6, $7 "
            "FROM using_devices d WHERE d.device_code = $1",
            device_code,
            int(binding["using_point_id"]),
            point_name or f"point-{point_id}",
            value,
            int(time.time()),
            issued_by,
            cmd_id,
        )

        if fut is None:
            return {"command_id": cmd_id, "accepted": True, "message": "queued"}

        try:
            result = await asyncio.wait_for(fut, timeout=timeout)
        except asyncio.TimeoutError:
            raise BizError(
                code=4102,
                message=f"control command timed out after {timeout:.1f}s",
                data={"command_id": cmd_id, "device_code": device_code, "point_id": point_id},
            ) from None
        finally:
            command_waiter.discard(key, fut)

        return {"command_id": cmd_id, "accepted": True, "finished": True, **result}

    async def get_command(self, command_id: int) -> dict[str, Any]:
        row = await self.pool.fetchrow(
            "SELECT * FROM control_commands WHERE ext_command_id = $1 "
            "ORDER BY id DESC LIMIT 1",
            command_id,
        )
        if row is None:
            raise BizError(code=404, message=f"command {command_id} not found")
        return dict(row)

    async def queue_depth(self) -> dict[str, Any]:
        health = self.collector.health()
        return {"queue_depth": health.get("queue_depth", 0), "revision": health.get("revision", 0)}

    @staticmethod
    def _validate_value(value: str, data_type: str | None) -> None:
        if not data_type:
            return
        dt = data_type.lower()
        try:
            if dt in ("int", "long"):
                int(value)
            elif dt in ("float", "double"):
                float(value)
            elif dt == "bool":
                if value.lower() not in ("0", "1", "true", "false", "yes", "no"):
                    raise ValueError()
        except (ValueError, TypeError):
            raise BizError(
                code=400,
                message=f"value '{value}' not parsable as {data_type}",
                data={"data_type": data_type, "value": value},
            )


def get_control_service(pool: DatabasePool) -> ControlService:
    collector = get_collector()
    return ControlService(pool, collector)
