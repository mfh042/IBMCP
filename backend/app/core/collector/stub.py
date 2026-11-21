from __future__ import annotations

import random
import time
from collections import deque
from typing import Any, Callable

from app.core.collector.protocol import (
    DeviceNotInPlan,
    InvalidConnection,
    NotInitialized,
    PlanRevisionConflict,
    ProtocolNotSupported,
)


class StubCollector:
    """采集扩展桩实现（Windows 默认用）。

    严格复刻真实扩展的校验语义与错误码（rc 字段 + stage/device_key/point_id 属性），
    让 Windows 开发侧的全链路可以跑到和 openEuler 生产侧完全一致的错误分支。

    不做真实 IO，snapshot_since 返回模拟递增数据。
    """

    SUPPORTED_PROTOCOLS = [
        "modbus_tcp",
        "modbus_rtu",
        "opc_ua",
        "mqtt",
        "socket_tcp",
        "socket_udp",
        "websocket",
    ]

    def __init__(self) -> None:
        self._revision: int = 0
        self._plan: dict[str, Any] | None = None
        self._seq: int = 0
        self._samples: deque[dict[str, Any]] = deque(maxlen=10000)
        self._cb: Callable[[dict[str, Any]], None] | None = None
        self._running: bool = False
        self._tick_count: int = 0

    def init(self, cfg: dict[str, Any] | None = None) -> None:
        self._running = True

    def health(self) -> dict[str, Any]:
        return {
            "abi": "1.0",
            "lib": "stub-0.1.0",
            "protocols": list(self.SUPPORTED_PROTOCOLS),
            "threads": 0,
            "revision": self._revision,
            "device_count": len(self._plan.get("devices", [])) if self._plan else 0,
            "point_count": sum(
                len(d.get("points", [])) for d in (self._plan or {}).get("devices", [])
            ),
            "queue_depth": 0,
            "dropped": 0,
        }

    def list_protocols(self) -> list[str]:
        return list(self.SUPPORTED_PROTOCOLS)

    def protocol_capabilities(self) -> dict[str, dict[str, Any]]:
        return {
            p: {"supports_write": True, "max_points_per_device": 512}
            for p in self.SUPPORTED_PROTOCOLS
        }

    def apply_plan(self, plan: dict[str, Any], revision: int) -> int:
        if not self._running:
            raise NotInitialized(
                "collector not initialized, call init() first",
                rc=4,
                stage="apply_plan",
            )

        self._validate_plan(plan)

        if self._plan is not None and revision == self._revision:
            raise PlanRevisionConflict(
                f"revision {revision} already applied with different plan",
                rc=3,
                stage="apply_plan",
            )

        if revision < self._revision:
            raise PlanRevisionConflict(
                f"revision {revision} is older than current {self._revision}",
                rc=3,
                stage="apply_plan",
            )

        self._revision = revision
        self._plan = plan
        self._emit_event(
            {
                "type": "plan_ack",
                "revision": revision,
                "device_count": len(plan.get("devices", [])),
            }
        )
        now_ms = int(time.time() * 1000)
        for dev in plan.get("devices", []):
            self._emit_event(
                {
                    "type": "device_status",
                    "device_key": dev.get("device_key", ""),
                    "status": 1,
                    "reason": "connected",
                    "ts_ms": now_ms,
                }
            )
        return 0

    def set_event_callback(self, cb: Callable[[dict[str, Any]], None] | None) -> None:
        self._cb = cb

    def snapshot_since(self, since_seq: int) -> tuple[int, list[dict[str, Any]]]:
        if not self._running:
            raise NotInitialized(
                "collector not initialized",
                rc=4,
                stage="snapshot_since",
            )
        self._tick()
        samples = [s for s in self._samples if s["seq"] > since_seq]
        return self._seq, samples

    def write_point(self, device_key: str, point_id: int, value: str) -> int:
        if not self._running:
            raise NotInitialized(
                "collector not initialized",
                rc=4,
                stage="write_point",
            )

        if self._plan is None:
            raise DeviceNotInPlan(
                f"no active plan, cannot write {device_key}:{point_id}",
                rc=6,
                stage="write_point",
                device_key=device_key,
                point_id=point_id,
            )

        found = False
        for dev in self._plan.get("devices", []):
            if dev.get("device_key") != device_key:
                continue
            for pt in dev.get("points", []):
                if pt.get("point_id") == point_id:
                    found = True
                    break
            if found:
                break

        if not found:
            raise DeviceNotInPlan(
                f"device={device_key} point={point_id} not in active plan",
                rc=6,
                stage="write_point",
                device_key=device_key,
                point_id=point_id,
            )

        cmd_id = random.randint(1_000_000, 9_999_999)
        self._emit_event(
            {
                "type": "write_result",
                "command_id": cmd_id,
                "device_key": device_key,
                "point_id": point_id,
                "success": True,
                "status": "ok",
                "value": value,
                "finished_at": int(time.time()),
            }
        )
        return cmd_id

    def shutdown(self) -> None:
        if self._plan is not None and self._cb is not None:
            now_ms = int(time.time() * 1000)
            for dev in self._plan.get("devices", []):
                self._emit_event(
                    {
                        "type": "device_status",
                        "device_key": dev.get("device_key", ""),
                        "status": 0,
                        "reason": "collector shutdown",
                        "ts_ms": now_ms,
                    }
                )
        self._running = False
        self._plan = None

    # --- 内部 ---

    def _validate_plan(self, plan: dict[str, Any]) -> None:
        if not isinstance(plan, dict):
            raise InvalidConnection(
                "plan must be a dict",
                rc=2,
                stage="apply_plan.validate",
            )
        devices = plan.get("devices")
        if not isinstance(devices, list):
            raise InvalidConnection(
                "plan.devices must be a list",
                rc=2,
                stage="apply_plan.validate",
            )
        for dev in devices:
            proto = dev.get("protocol")
            dk = dev.get("device_key", "")
            if proto and proto not in self.SUPPORTED_PROTOCOLS:
                raise ProtocolNotSupported(
                    f"protocol not supported: {proto}",
                    rc=1,
                    stage="apply_plan.validate",
                    device_key=dk,
                )
            points = dev.get("points", [])
            for pt in points:
                addr = pt.get("address")
                if not addr:
                    raise InvalidConnection(
                        f"point address is empty (point_id={pt.get('point_id')})",
                        rc=2,
                        stage="apply_plan.validate",
                        device_key=dk,
                        point_id=pt.get("point_id", 0),
                    )
                scale = pt.get("scale")
                if scale is not None and float(scale) == 0.0:
                    raise InvalidConnection(
                        f"scale must not be 0 (point_id={pt.get('point_id')})",
                        rc=2,
                        stage="apply_plan.validate",
                        device_key=dk,
                        point_id=pt.get("point_id", 0),
                    )

    def _emit_event(self, evt: dict[str, Any]) -> None:
        if self._cb is not None:
            try:
                self._cb(evt)
            except Exception:
                pass

    def _tick(self) -> None:
        if not self._plan or not self._running:
            return
        now_ms = int(time.time() * 1000)
        for dev in self._plan.get("devices", []):
            dk = dev.get("device_key", "")
            for pt in dev.get("points", []):
                self._seq += 1
                self._samples.append(
                    {
                        "seq": self._seq,
                        "device_key": dk,
                        "point_id": pt.get("point_id"),
                        "value": round(random.uniform(0, 100), 2),
                        "quality": "Good",
                        "ts_ms": now_ms,
                    }
                )
        self._tick_count += 1
        if self._tick_count % 10 == 0:
            self._emit_event(
                {
                    "type": "collector_stats",
                    "revision": self._revision,
                    "total_samples": self._seq,
                    "devices_online": len(self._plan.get("devices", [])),
                }
            )


__all__ = ["StubCollector"]
