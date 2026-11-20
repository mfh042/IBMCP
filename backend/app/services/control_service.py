from __future__ import annotations

import logging
import time
from typing import Any

from app.core.collector.ffi import get_collector
from app.core.collector.protocol import CollectorApi
from app.core.errors import BizError
from app.db.pool import DatabasePool

logger = logging.getLogger(__name__)


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
    ) -> dict[str, Any]:
        binding = await self.pool.fetchrow(
            "SELECT rc.id AS binding_id, rc.device_code, rc.protocol_code, "
            "col.read_write, col.data_type, col.item_code "
            "FROM running_route_device rc "
            "JOIN running_device_collection col ON col.binding_id = rc.id "
            "WHERE rc.device_code = $1 AND col.using_point_id = $2 "
            "AND rc.enabled = 1 AND col.enabled = 1",
            device_code,
            point_id,
        )
        if binding is None:
            raise BizError(code=404, message=f"device/point not in running config: {device_code}:{point_id}")

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

        try:
            cmd_id = self.collector.write_point(device_code, point_id, value)
        except Exception as exc:
            logger.exception("write_point failed device=%s point=%d", device_code, point_id)
            raise BizError(code=500, message=f"write rejected by collector: {exc}") from exc

        await self.pool.execute(
            "INSERT INTO control_commands "
            "(command_id, device_key, point_id, point_name, value, status, accepted_at) "
            "VALUES ($1, $2, $3, $4, $5, 0, $6)",
            cmd_id,
            device_code,
            point_id,
            point_name or f"point-{point_id}",
            value,
            int(time.time()),
        )

        return {"command_id": cmd_id, "accepted": True, "message": "queued"}

    async def get_command(self, command_id: int) -> dict[str, Any]:
        row = await self.pool.fetchrow(
            "SELECT * FROM control_commands WHERE command_id = $1",
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
