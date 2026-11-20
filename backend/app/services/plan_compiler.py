from __future__ import annotations

import hashlib
import json
import logging
from typing import Any

from app.db.pool import DatabasePool

logger = logging.getLogger(__name__)


def normalize_protocol(name: str) -> str:
    """协议名归一化（与 C++ 侧 DriverFactory::normalize 语义一致）。

    丢弃 `_ - 空格 .`，统一小写。
    MODBUS_TCP / Modbus TCP / modbus-tcp / modbus.tcp → "modbustcp"
    """
    return "".join(c for c in name.lower() if c not in "_- .")


class PlanCompiler:
    """五级配置链 → 采集计划编译。

    running_equipment_config (config_status=1)
      └─ running_equipment_route (enabled=1)
           └─ running_route_device (enabled=1)
                └─ running_device_collection (enabled=1)
                     ↓ 与 base_device_data_item_dict JOIN 取默认 scale/offset/alarm_*
                     ↓ address_override 为空 → address_rule
                     ↓ 不允许回退 item_code（02 号 S6）
                     ↓ normalize(protocol_code) 校验扩展支持
    """

    def __init__(self, pool: DatabasePool) -> None:
        self.pool = pool

    async def compile_for_config(self, config_id: int, revision: int) -> dict[str, Any]:
        config = await self.pool.fetchrow(
            "SELECT * FROM running_equipment_config WHERE id = $1 AND config_status = 1",
            config_id,
        )
        if config is None:
            raise ValueError(f"config {config_id} not found or not active")

        routes = await self.pool.fetch(
            "SELECT * FROM running_equipment_route WHERE config_id = $1 AND enabled = 1",
            config_id,
        )

        devices: list[dict[str, Any]] = []
        for route in routes:
            bindings = await self.pool.fetch(
                "SELECT * FROM running_route_device WHERE route_config_id = $1 AND enabled = 1",
                route["id"],
            )
            for b in bindings:
                collections = await self.pool.fetch(
                    "SELECT c.*, d.item_code, d.item_name, d.address_rule, "
                    "d.data_type, d.read_write, d.unit, "
                    "COALESCE(c.scale, d.scale) AS scale, "
                    "COALESCE(c.\"offset\", d.\"offset\") AS \"offset\", "
                    "COALESCE(c.alarm_high, d.alarm_high) AS alarm_high, "
                    "COALESCE(c.alarm_low, d.alarm_low) AS alarm_low, "
                    "COALESCE(c.alarm_enabled, d.alarm_enabled) AS alarm_enabled "
                    "FROM running_device_collection c "
                    "JOIN base_device_data_item_dict d ON d.item_code = c.item_code "
                    "WHERE c.binding_id = $1 AND c.enabled = 1",
                    b["id"],
                )
                points: list[dict[str, Any]] = []
                for c in collections:
                    address = (
                            c["address_override"]
                            or c["address_rule"]
                            or ""
                    )
                    if not address:
                        raise ValueError(
                            f"point {c['item_code']} on device {b['device_code']} "
                            f"has empty address (neither address_override nor address_rule)"
                        )
                    scale = float(c["scale"]) if c["scale"] is not None else 1.0
                    if scale == 0.0:
                        raise ValueError(
                            f"scale must not be 0 for item {c['item_code']} "
                            f"on device {b['device_code']}"
                        )
                    cycle_ms = int(c["collection_cycle_ms"] or 1000)

                    points.append(
                        {
                            "point_id": int(c["using_point_id"]) if c.get("using_point_id") else 0,
                            "point_key": f"{b['device_code']}:{c.get('using_point_id', c['item_code'])}",
                            "item_code": c["item_code"],
                            "item_name": c.get("item_name", ""),
                            "address": address,
                            "data_type": c.get("data_type", "float"),
                            "read_write": c.get("read_write", "R"),
                            "cycle_ms": cycle_ms,
                            "scale": scale,
                            "offset": float(c["offset"]) if c.get("offset") is not None else 0.0,
                            "alarm_high": float(c["alarm_high"]) if c.get("alarm_high") is not None else 0.0,
                            "alarm_low": float(c["alarm_low"]) if c.get("alarm_low") is not None else 0.0,
                            "alarm_enabled": bool(c.get("alarm_enabled", False)),
                            "unit": c.get("unit", ""),
                        }
                    )

                protocol_code = b["protocol_code"]
                devices.append(
                    {
                        "device_key": b["device_code"],
                        "device_name": b.get("device_name") or b["device_code"],
                        "equipment_code": config["config_code"],
                        "route_code": route["route_code"],
                        "step_code": b.get("step_code"),
                        "protocol": protocol_code.lower(),
                        "connection": b.get("device_connection") or {},
                        "connect_timeout_ms": int(b.get("connect_timeout_ms") or 3000),
                        "read_timeout_ms": int(b.get("read_timeout_ms") or 1000),
                        "points": points,
                    }
                )

        plan = {
            "revision": revision,
            "flush_interval_ms": 200,
            "devices": devices,
        }
        plan["plan_hash"] = hashlib.sha256(
            json.dumps(
                {k: v for k, v in plan.items() if k != "revision"},
                sort_keys=True,
                default=str,
            ).encode()
        ).hexdigest()[:16]
        return plan

    async def compile_full(self, revision: int) -> dict[str, Any]:
        configs = await self.pool.fetch(
            "SELECT id FROM running_equipment_config WHERE config_status = 1",
        )
        all_devices: list[dict[str, Any]] = []
        for cfg in configs:
            partial = await self.compile_for_config(cfg["id"], revision)
            all_devices.extend(partial["devices"])
        plan = {
            "revision": revision,
            "flush_interval_ms": 200,
            "devices": all_devices,
        }
        plan["plan_hash"] = hashlib.sha256(
            json.dumps(
                {k: v for k, v in plan.items() if k != "revision"},
                sort_keys=True,
                default=str,
            ).encode()
        ).hexdigest()[:16]
        return plan
