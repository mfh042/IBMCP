from __future__ import annotations

import logging
from typing import Any

from app.core.collector.protocol import (
    CLASS_TO_RC,
    RC_TO_CODE,
    CollectorApi,
    CollectorError,
    to_api_error,
)

logger = logging.getLogger(__name__)


class NativeCollector:
    def __init__(self) -> None:
        self._core: Any | None = None

    def _ensure_loaded(self) -> Any:
        if self._core is not None:
            return self._core
        try:
            import industrial_collector._core as _core  # type: ignore[import-not-found]

            self._core = _core
            return _core
        except ImportError as exc:
            raise RuntimeError(
                "industrial_collector._core 未安装，请在 openEuler 侧执行 "
                "`pip install ./python ./backend`（Windows 侧默认用 stub）"
            ) from exc

    def init(self, cfg: dict[str, Any] | None = None) -> None:
        self._ensure_loaded()
        try:
            self._core.init(cfg or {})
        except Exception as exc:
            logger.exception("collector.init failed")
            raise CollectorError(str(exc), rc=getattr(exc, "rc", 0)) from exc

    def health(self) -> dict[str, Any]:
        self._ensure_loaded()
        try:
            return dict(self._core.health())
        except Exception as exc:
            logger.exception("collector.health failed")
            raise CollectorError(str(exc), rc=getattr(exc, "rc", 0)) from exc

    def list_protocols(self) -> list[str]:
        self._ensure_loaded()
        try:
            return list(self._core.list_protocols())
        except Exception as exc:
            logger.exception("collector.list_protocols failed")
            raise CollectorError(str(exc), rc=getattr(exc, "rc", 0)) from exc

    def protocol_capabilities(self) -> dict[str, dict[str, Any]]:
        self._ensure_loaded()
        try:
            caps = self._core.protocol_capabilities()
            return {k: dict(v) for k, v in caps.items()}
        except Exception as exc:
            logger.exception("collector.protocol_capabilities failed")
            raise CollectorError(str(exc), rc=getattr(exc, "rc", 0)) from exc

    def apply_plan(self, plan: dict[str, Any], revision: int) -> int:
        self._ensure_loaded()
        try:
            return int(self._core.apply_plan(plan, revision))
        except Exception as exc:
            logger.exception("collector.apply_plan failed revision=%d", revision)
            raise CollectorError(
                str(exc),
                rc=getattr(exc, "rc", 0),
                stage=getattr(exc, "stage", ""),
                device_key=getattr(exc, "device_key", ""),
                point_id=getattr(exc, "point_id", 0),
            ) from exc

    def set_event_callback(self, cb) -> None:
        self._ensure_loaded()
        try:
            self._core.set_event_callback(cb)
        except Exception as exc:
            logger.exception("collector.set_event_callback failed")
            raise CollectorError(str(exc), rc=getattr(exc, "rc", 0)) from exc

    def snapshot_since(self, since_seq: int) -> tuple[int, list[dict[str, Any]]]:
        self._ensure_loaded()
        try:
            new_seq, snapshots = self._core.snapshot_since(since_seq)
            return int(new_seq), [dict(s) for s in snapshots]
        except Exception as exc:
            logger.exception("collector.snapshot_since failed since=%d", since_seq)
            raise CollectorError(str(exc), rc=getattr(exc, "rc", 0)) from exc

    def write_point(self, device_key: str, point_id: int, value: str) -> int:
        self._ensure_loaded()
        try:
            return int(self._core.write_point(device_key, point_id, value))
        except Exception as exc:
            logger.exception(
                "collector.write_point failed device=%s point=%d",
                device_key,
                point_id,
            )
            raise CollectorError(
                str(exc),
                rc=getattr(exc, "rc", 0),
                stage=getattr(exc, "stage", ""),
                device_key=device_key,
                point_id=point_id,
            ) from exc

    def shutdown(self) -> None:
        if self._core is not None:
            try:
                self._core.shutdown()
            except Exception as exc:
                logger.exception("collector.shutdown failed")


_stub_instance: CollectorApi | None = None
_native_instance: CollectorApi | None = None


def get_collector(backend: str = "stub") -> CollectorApi:
    global _stub_instance, _native_instance
    if backend == "stub":
        if _stub_instance is None:
            from app.core.collector.stub import StubCollector

            _stub_instance = StubCollector()
        return _stub_instance
    if backend == "native":
        if _native_instance is None:
            _native_instance = NativeCollector()
        return _native_instance
    raise ValueError(f"unknown collector backend: {backend}")


__all__ = [
    "NativeCollector",
    "CollectorApi",
    "CollectorError",
    "RC_TO_CODE",
    "CLASS_TO_RC",
    "to_api_error",
    "get_collector",
]
