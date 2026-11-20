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
        """FastAPI 启动时对比 DB 最后 revision，补发一次 apply_plan。"""
        row = await self.pool.fetchrow(
            "SELECT revision, plan_hash FROM runtime_plan_revision ORDER BY revision DESC LIMIT 1",
        )
        if row and int(row["revision"]) > 0:
            logger.info(
                "CollectorOrchestrator.bootstrap skip revision=%s hash=%s already applied",
                row["revision"],
                row["plan_hash"],
            )
            return

        last_rev = int(row["revision"]) if row else 0
        plan = await self.compiler.compile_full(revision=last_rev + 1)
        new_rev = plan["revision"]
        plan_hash = plan.get("plan_hash") or self._plan_hash(plan)

        logger.info(
            "CollectorOrchestrator.bootstrap revision=%d devices=%d",
            new_rev,
            len(plan["devices"]),
        )

        try:
            ret = self.collector.apply_plan(plan, new_rev)
        except Exception:
            logger.exception("bootstrap apply_plan failed")
            raise

        if ret != 0:
            raise BizError(code=409, message="bootstrap apply_plan rejected", data={"revision": new_rev})

        await self.pool.execute(
            "INSERT INTO runtime_plan_revision "
            "(revision, config_id, plan_hash, status, applied_at) "
            "VALUES ($1, NULL, $2, 1, $3)",
            new_rev,
            plan_hash,
            int(time.time()),
        )

    async def activate_config(self, config_id: int) -> dict[str, Any]:
        """激活一条配置：编译 → 比对 hash → apply_plan → 写 revision。

        幂等：若新 plan_hash 与当前生效相同，直接返回当前 revision（不重下发）。
        """
        await self.pool.execute(
            "UPDATE running_equipment_config SET config_status = 1, updated_at = $2 WHERE id = $1",
            config_id,
            int(time.time()),
        )

        row = await self.pool.fetchrow(
            "SELECT revision, plan_hash FROM runtime_plan_revision ORDER BY revision DESC LIMIT 1",
        )
        current_hash = row["plan_hash"] if row else None

        new_rev = int(row["revision"]) + 1 if row else 1
        plan = await self.compiler.compile_for_config(config_id, revision=new_rev)
        plan_hash = plan.get("plan_hash") or self._plan_hash(plan)

        if current_hash == plan_hash:
            return {
                "revision": int(row["revision"]) if row else 0,
                "skipped": True,
                "message": "no change",
            }

        try:
            ret = self.collector.apply_plan(plan, new_rev)
        except Exception:
            logger.exception("activate_config apply_plan failed config=%d", config_id)
            raise

        if ret != 0:
            await self.pool.execute(
                "UPDATE running_equipment_config SET config_status = 0 WHERE id = $1",
                config_id,
            )
            raise BizError(
                code=4000 + ret,
                message="apply_plan rejected by collector",
                data={"revision": new_rev, "extension_error": ret},
            )

        await self.pool.execute(
            "INSERT INTO runtime_plan_revision "
            "(revision, config_id, plan_hash, status, applied_at) "
            "VALUES ($1, $2, $3, 1, $4)",
            new_rev,
            config_id,
            plan_hash,
            int(time.time()),
        )

        logger.info(
            "CollectorOrchestrator.activate_config revision=%d config=%d devices=%d",
            new_rev,
            config_id,
            len(plan["devices"]),
        )

        return {"revision": new_rev, "plan_hash": plan_hash}

    async def compile_only(self, config_id: int) -> dict[str, Any]:
        """只编译不下发（/plan 预览端点用）。"""
        row = await self.pool.fetchrow(
            "SELECT COALESCE(MAX(revision), 0) AS rev FROM runtime_plan_revision",
        )
        rev = int(row["rev"]) + 1
        plan = await self.compiler.compile_for_config(config_id, revision=rev)
        return plan

    async def recompile_all(self) -> dict[str, Any]:
        """全量重编译并重下发（运维兜底）。"""
        row = await self.pool.fetchrow(
            "SELECT COALESCE(MAX(revision), 0) AS rev FROM runtime_plan_revision",
        )
        new_rev = int(row["rev"]) + 1
        plan = await self.compiler.compile_full(revision=new_rev)
        plan_hash = plan.get("plan_hash") or self._plan_hash(plan)

        try:
            ret = self.collector.apply_plan(plan, new_rev)
        except Exception:
            logger.exception("recompile apply_plan failed")
            raise

        if ret != 0:
            raise BizError(code=4000 + ret, message="apply_plan rejected", data={"revision": new_rev})

        await self.pool.execute(
            "INSERT INTO runtime_plan_revision "
            "(revision, config_id, plan_hash, status, applied_at) "
            "VALUES ($1, NULL, $2, 1, $3)",
            new_rev,
            plan_hash,
            int(time.time()),
        )
        return {"revision": new_rev, "device_count": len(plan["devices"])}

    @staticmethod
    def _plan_hash(plan: dict[str, Any]) -> str:
        payload = {k: v for k, v in plan.items() if k != "revision"}
        return hashlib.sha256(
            json.dumps(payload, sort_keys=True, default=str).encode()
        ).hexdigest()[:16]
