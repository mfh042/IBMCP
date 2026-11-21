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


def _json_value(value: Any, default: Any) -> Any:
    """asyncpg 默认把 JSON/JSONB 读成字符串，这里统一还原为 dict/list。"""
    if value is None:
        return default
    if isinstance(value, (dict, list)):
        return value
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return default
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            logger.warning("invalid json value from db: %.80s", text)
            return default
    return default


# 五级配置链全展开（对齐 02 号 §4.6，单 config 一次查询，禁止 N+1）
_COMPILE_SQL = """
SELECT c.id              AS config_id,
       c.config_code     AS config_code,
       c.equipment_code  AS equipment_code,
       c.equipment_name  AS equipment_name,
       r.route_code      AS route_code,
       r.id              AS route_config_id,
       d.id              AS binding_id,
       d.step_code       AS step_code,
       d.device_code     AS device_code,
       d.device_name     AS device_name,
       d.protocol_code   AS protocol_code,
       d.device_connection AS device_connection,
       d.connect_timeout_ms AS connect_timeout_ms,
       d.read_timeout_ms AS read_timeout_ms,
       col.id            AS collection_id,
       col.item_code     AS item_code,
       COALESCE(col.item_name, it.item_name)                                  AS item_name,
       COALESCE(NULLIF(col.address_override, ''), it.address_rule)            AS address,
       COALESCE(col.collection_cycle, it.collection_cycle, 1) * 1000          AS cycle_ms,
       COALESCE(col.data_type, it.data_type, 'float')                         AS data_type,
       COALESCE(col.read_write, it.read_write, 'R')                           AS read_write,
       COALESCE(col.scale, it.scale, 1.0)                                     AS scale,
       COALESCE(col."offset", it."offset", 0.0)                               AS "offset",
       COALESCE(col.alarm_high, it.alarm_high, 0.0)                           AS alarm_high,
       COALESCE(col.alarm_low, it.alarm_low, 0.0)                             AS alarm_low,
       COALESCE(col.alarm_enabled, it.alarm_enabled, false)                   AS alarm_enabled,
       COALESCE(it.unit, '')                                                  AS unit
FROM running_equipment_config c
JOIN running_equipment_route r     ON r.config_id = c.id AND r.enabled = 1
JOIN running_route_device d        ON d.route_config_id = r.id AND d.enabled = 1
JOIN running_device_collection col ON col.device_binding_id = d.id AND col.enabled = 1
JOIN base_device_data_item_dict it ON it.item_code = col.item_code
{where}
ORDER BY d.id, col.id
"""


class PlanCompiler:
    """五级配置链 → 采集计划编译。

    running_equipment_config (config_status=1)
      └─ running_equipment_route (enabled=1)
           └─ running_route_device (enabled=1)
                └─ running_device_collection (enabled=1)
                     ↓ 与 base_device_data_item_dict JOIN 取默认 scale/offset/alarm_*
                     ↓ address_override 为空 → address_rule
                     ↓ 不允许回退 item_code（02 号 S6）
                     ↓ collection_cycle（秒）→ cycle_ms
    """

    def __init__(self, pool: DatabasePool) -> None:
        self.pool = pool

    async def compile_for_config(self, config_id: int, revision: int) -> dict[str, Any]:
        rows = await self.pool.fetch(
            _COMPILE_SQL.format(where="WHERE c.id = $1 AND c.config_status = 1"),
            config_id,
        )
        if not rows:
            exists = await self.pool.fetchrow(
                "SELECT config_status FROM running_equipment_config WHERE id = $1",
                config_id,
            )
            if exists is None:
                raise ValueError(f"config {config_id} not found")
            if int(exists["config_status"]) != 1:
                raise ValueError(f"config {config_id} is not active (config_status != 1)")
            logger.warning("config %s is active but produced an empty plan", config_id)

        return self._build_plan(rows, revision)

    async def compile_full(self, revision: int) -> dict[str, Any]:
        rows = await self.pool.fetch(
            _COMPILE_SQL.format(where="WHERE c.config_status = 1"),
        )
        return self._build_plan(rows, revision)

    # --- 内部 ---

    def _build_plan(self, rows: list[Any], revision: int) -> dict[str, Any]:
        devices: dict[str, dict[str, Any]] = {}

        for row in rows:
            device_key = row["device_code"]
            address = (row["address"] or "").strip()
            if not address:
                raise ValueError(
                    f"point {row['item_code']} on device {device_key} has empty address "
                    f"(neither address_override nor data_item.address_rule)"
                )

            scale = float(row["scale"]) if row["scale"] is not None else 1.0
            if scale == 0.0:
                raise ValueError(
                    f"scale must not be 0 for item {row['item_code']} on device {device_key}"
                )

            device = devices.get(device_key)
            if device is None:
                device = {
                    "device_key": device_key,
                    "device_name": row["device_name"] or device_key,
                    "equipment_code": row["equipment_code"] or "",
                    "equipment_name": row["equipment_name"] or "",
                    "route_code": row["route_code"],
                    "step_code": row["step_code"],
                    "protocol": (row["protocol_code"] or "").lower(),
                    "connection": _json_value(row["device_connection"], {}),
                    "connect_timeout_ms": int(row["connect_timeout_ms"] or 3000),
                    "read_timeout_ms": int(row["read_timeout_ms"] or 1000),
                    "points": [],
                }
                devices[device_key] = device
            else:
                logger.warning(
                    "device %s appears in multiple routes/configs, merging points", device_key
                )

            point = {
                "point_id": int(row["collection_id"]),
                "point_key": f"{device_key}:{row['collection_id']}",
                "item_code": row["item_code"],
                "item_name": row["item_name"] or "",
                "address": address,
                "data_type": row["data_type"] or "float",
                "read_write": row["read_write"] or "R",
                "cycle_ms": int(row["cycle_ms"] or 1000),
                "scale": scale,
                "offset": float(row["offset"]) if row["offset"] is not None else 0.0,
                "alarm_high": float(row["alarm_high"]) if row["alarm_high"] is not None else 0.0,
                "alarm_low": float(row["alarm_low"]) if row["alarm_low"] is not None else 0.0,
                "alarm_enabled": bool(row["alarm_enabled"]),
                "unit": row["unit"] or "",
            }

            existing = next(
                (p for p in device["points"]
                 if p["address"] == point["address"] and p["data_type"] == point["data_type"]),
                None,
            )
            if existing is None:
                device["points"].append(point)
            else:
                # 同一设备同一地址被多条配置重复绑定时，保留最短采集周期
                existing["cycle_ms"] = min(existing["cycle_ms"], point["cycle_ms"])

        plan = {
            "revision": revision,
            "flush_interval_ms": 200,
            "devices": list(devices.values()),
        }
        plan["plan_hash"] = self._plan_hash(plan)
        return plan

    @staticmethod
    def _plan_hash(plan: dict[str, Any]) -> str:
        payload = {k: v for k, v in plan.items() if k != "revision"}
        return hashlib.sha256(
            json.dumps(payload, sort_keys=True, default=str).encode()
        ).hexdigest()[:16]
