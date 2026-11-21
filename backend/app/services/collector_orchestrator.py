from __future__ import annotations

import hashlib
import json
import logging
import time
from typing import Any

from app.core.collector.protocol import CollectorApi
from app.core.errors import BizError
from app.db.pool import DatabasePool
from app.services.plan_compiler import PlanCompiler

logger = logging.getLogger(__name__)


class CollectorOrchestrator:
    """扩展的计划下发 / 回执 / 版本幂等编排（对齐 01 号 5.3 状态机）。"""

    def __init__(self, pool: DatabasePool, collector: CollectorApi) -> None:
        self.pool = pool
        self.collector = collector
        self.compiler = PlanCompiler(pool)

    async def bootstrap(self) -> None:
        """进程启动时重放当前生效配置（01 号 7.7 无状态重启）。

        扩展进程内实例在重启后是空的，因此不能因 DB 已有 revision 就跳过下发。
        """
        row = await self.pool.fetchrow(
            "SELECT COALESCE(MAX(revision), 0) AS rev FROM runtime_plan_revision",
        )
        new_rev = int(row["rev"]) + 1
        plan = await self._compile_full(new_rev)

        logger.info(
            "CollectorOrchestrator.bootstrap revision=%d devices=%d",
            new_rev,
            len(plan["devices"]),
        )
        await self._apply(plan, config_id=None)

    async def activate_config(self, config_id: int) -> dict[str, Any]:
        """激活一条配置：置位 → 全量编译 → hash 比对 → apply_plan → 写 revision。

        计划始终为“全部生效配置”的全量结果，避免激活 A 时把 B 的设备挤出计划。
        """
        exists = await self.pool.fetchrow(
            "SELECT id FROM running_equipment_config WHERE id = $1", config_id,
        )
        if exists is None:
            raise BizError(code=404, message="config not found")

        await self.pool.execute(
            "UPDATE running_equipment_config SET config_status = 1, updated_at = $2 WHERE id = $1",
            config_id,
            int(time.time()),
        )

        row = await self.pool.fetchrow(
            "SELECT COALESCE(MAX(revision), 0) AS rev, "
            "(SELECT plan_hash FROM runtime_plan_revision ORDER BY revision DESC LIMIT 1) AS last_hash "
            "FROM runtime_plan_revision",
        )
        new_rev = int(row["rev"]) + 1

        try:
            plan = await self._compile_full(new_rev)
        except BizError:
            await self._set_status(config_id, 0)
            raise

        if row["last_hash"] == plan["plan_hash"]:
            return {
                "revision": int(row["rev"]),
                "skipped": True,
                "message": "no change",
            }

        try:
            await self._apply(plan, config_id=config_id)
        except Exception:
            await self._set_status(config_id, 0)
            raise

        logger.info(
            "CollectorOrchestrator.activate_config revision=%d config=%d devices=%d",
            new_rev,
            config_id,
            len(plan["devices"]),
        )
        return {"revision": new_rev, "plan_hash": plan["plan_hash"]}

    async def deactivate_config(self, config_id: int) -> dict[str, Any]:
        """停用配置：状态置 2 → 全量重编（排除该配置）→ 下发。"""
        exists = await self.pool.fetchrow(
            "SELECT id FROM running_equipment_config WHERE id = $1", config_id,
        )
        if exists is None:
            raise BizError(code=404, message="config not found")

        await self._set_status(config_id, 2)
        new_rev = await self._next_revision()
        plan = await self._compile_full(new_rev)
        await self._apply(plan, config_id=config_id)
        return {
            "revision": new_rev,
            "device_count": len(plan["devices"]),
            "message": "config deactivated",
        }

    async def compile_only(self, config_id: int) -> dict[str, Any]:
        """只编译不下发（/plan 预览端点用）。"""
        new_rev = await self._next_revision()
        try:
            return await self.compiler.compile_for_config(config_id, revision=new_rev)
        except ValueError as exc:
            raise BizError(code=422, message=str(exc)) from exc

    async def recompile_all(self) -> dict[str, Any]:
        """全量重编译并重下发（运维兜底）。"""
        new_rev = await self._next_revision()
        plan = await self._compile_full(new_rev)
        await self._apply(plan, config_id=None)
        return {"revision": new_rev, "device_count": len(plan["devices"])}

    # --- 内部 ---

    async def _next_revision(self) -> int:
        row = await self.pool.fetchrow(
            "SELECT COALESCE(MAX(revision), 0) AS rev FROM runtime_plan_revision",
        )
        return int(row["rev"]) + 1

    async def _compile_full(self, revision: int) -> dict[str, Any]:
        try:
            return await self.compiler.compile_full(revision)
        except ValueError as exc:
            raise BizError(code=422, message=f"plan compile failed: {exc}") from exc

    async def _set_status(self, config_id: int, status: int) -> None:
        await self.pool.execute(
            "UPDATE running_equipment_config SET config_status = $2, updated_at = $3 WHERE id = $1",
            config_id,
            status,
            int(time.time()),
        )

    async def _apply(self, plan: dict[str, Any], config_id: int | None) -> None:
        revision = int(plan["revision"])
        plan_hash = plan.get("plan_hash") or self._plan_hash(plan)

        ret = self.collector.apply_plan(plan, revision)
        if ret != 0:
            raise BizError(
                code=4000 + ret,
                message="apply_plan rejected by collector",
                data={"revision": revision, "extension_error": ret},
            )

        await self._sync_runtime_snapshot(plan)

        await self.pool.execute(
            "INSERT INTO runtime_plan_revision "
            "(revision, config_id, plan_hash, status, applied_at) "
            "VALUES ($1, $2, $3, 1, $4)",
            revision,
            config_id,
            plan_hash,
            int(time.time()),
        )

    async def _sync_runtime_snapshot(self, plan: dict[str, Any]) -> None:
        """plan_ack 后把计划投影到 using_devices / using_points（02 号 §5.1/§5.2）。

        `using_points.using_point_id` 保存 plan 的点位主键（= running_device_collection.id），
        采样落库时据此映射到本表自增 id（time_series_data.point_id 的外键目标）。
        """
        now = int(time.time())
        conn = await self.pool.pool().acquire()
        try:
            async with conn.transaction():
                for dev in plan.get("devices", []):
                    device_code = dev["device_key"]
                    await conn.execute(
                        "INSERT INTO using_devices "
                        "(device_code, device_name, equipment_code, equipment_name, protocol_code, "
                        " device_connection, status, created_at, updated_at) "
                        "VALUES ($1, $2, $3, $4, $5, $6::jsonb, 0, $7, $7) "
                        "ON CONFLICT (device_code) DO UPDATE SET "
                        " device_name = EXCLUDED.device_name, "
                        " equipment_code = EXCLUDED.equipment_code, "
                        " equipment_name = EXCLUDED.equipment_name, "
                        " protocol_code = EXCLUDED.protocol_code, "
                        " device_connection = EXCLUDED.device_connection, "
                        " updated_at = EXCLUDED.updated_at",
                        device_code,
                        dev.get("device_name") or device_code,
                        dev.get("equipment_code") or "",
                        dev.get("equipment_name") or "",
                        (dev.get("protocol") or "").upper(),
                        json.dumps(dev.get("connection") or {}, default=str),
                        now,
                    )

                    addresses: list[str] = []
                    for pt in dev.get("points", []):
                        address = pt["address"]
                        addresses.append(address)
                        cycle_ms = int(pt.get("cycle_ms") or 1000)
                        await conn.execute(
                            "INSERT INTO using_points "
                            "(device_code, using_point_id, item_name, address, data_type, read_write, "
                            " unit, scale, \"offset\", collection_cycle, cycle_ms, "
                            " alarm_high, alarm_low, alarm_enabled, enabled, created_at, updated_at) "
                            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, true, $15, $15) "
                            "ON CONFLICT (device_code, address) DO UPDATE SET "
                            " using_point_id = EXCLUDED.using_point_id, "
                            " item_name = EXCLUDED.item_name, "
                            " data_type = EXCLUDED.data_type, "
                            " read_write = EXCLUDED.read_write, "
                            " unit = EXCLUDED.unit, "
                            " scale = EXCLUDED.scale, "
                            " \"offset\" = EXCLUDED.\"offset\", "
                            " collection_cycle = EXCLUDED.collection_cycle, "
                            " cycle_ms = EXCLUDED.cycle_ms, "
                            " alarm_high = EXCLUDED.alarm_high, "
                            " alarm_low = EXCLUDED.alarm_low, "
                            " alarm_enabled = EXCLUDED.alarm_enabled, "
                            " enabled = true, "
                            " updated_at = EXCLUDED.updated_at",
                            device_code,
                            int(pt["point_id"]),
                            pt.get("item_name") or pt.get("item_code") or address,
                            address,
                            pt.get("data_type") or "float",
                            pt.get("read_write") or "R",
                            pt.get("unit") or "",
                            float(pt.get("scale") or 1.0),
                            float(pt.get("offset") or 0.0),
                            max(1, cycle_ms // 1000),
                            cycle_ms,
                            float(pt.get("alarm_high") or 0.0),
                            float(pt.get("alarm_low") or 0.0),
                            bool(pt.get("alarm_enabled")),
                            now,
                        )

                    # 不再出现在计划中的点位只停用，不物理删除（保留历史时序与报警）
                    await conn.execute(
                        "UPDATE using_points SET enabled = false, updated_at = $2 "
                        "WHERE device_code = $1 AND NOT (address = ANY($3::text[]))",
                        device_code,
                        now,
                        addresses,
                    )
        finally:
            await self.pool.pool().release(conn)

        logger.info(
            "runtime snapshot synced revision=%s devices=%d",
            plan.get("revision"),
            len(plan.get("devices", [])),
        )

    @staticmethod
    def _plan_hash(plan: dict[str, Any]) -> str:
        payload = {k: v for k, v in plan.items() if k != "revision"}
        return hashlib.sha256(
            json.dumps(payload, sort_keys=True, default=str).encode()
        ).hexdigest()[:16]
