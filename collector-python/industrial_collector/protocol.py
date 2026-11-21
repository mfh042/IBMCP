from typing import Any, Callable, Protocol

RC_TO_CODE: dict[int, int] = {0: 500, 1: 4001, 2: 4002, 3: 4003, 4: 4004, 5: 4005, 6: 4006}
CLASS_TO_RC: dict[str, int] = {
    "ProtocolNotSupported": 1,
    "InvalidConnection": 2,
    "PlanRevisionConflict": 3,
    "NotInitialized": 4,
    "ControlQueueFull": 5,
    "DeviceNotInPlan": 6,
}


def to_api_error(exc: BaseException) -> tuple[int, str, dict[str, Any]]:
    rc = getattr(exc, "rc", CLASS_TO_RC.get(type(exc).__name__, 0))
    extra = {
        k: getattr(exc, k)
        for k in ("stage", "device_key", "point_id")
        if getattr(exc, k, None) not in (None, "")
    }
    return RC_TO_CODE.get(rc, 500), str(exc), extra


class CollectorError(Exception):
    rc: int = 0
    stage: str = ""
    device_key: str = ""
    point_id: int = 0

    def __init__(
            self,
            msg: str = "",
            rc: int = 0,
            stage: str = "",
            device_key: str = "",
            point_id: int = 0,
    ) -> None:
        super().__init__(msg)
        self.rc = rc
        self.stage = stage
        self.device_key = device_key
        self.point_id = point_id


class ProtocolNotSupported(CollectorError):
    rc = 1


class InvalidConnection(CollectorError):
    rc = 2


class PlanRevisionConflict(CollectorError):
    rc = 3


class NotInitialized(CollectorError):
    rc = 4


class ControlQueueFull(CollectorError):
    rc = 5


class DeviceNotInPlan(CollectorError):
    rc = 6


class CollectorApi(Protocol):
    def init(self, cfg: dict[str, Any] | None = None) -> None: ...

    def health(self) -> dict[str, Any]: ...

    def list_protocols(self) -> list[str]: ...

    def protocol_capabilities(self) -> dict[str, dict[str, Any]]: ...

    def apply_plan(self, plan: dict[str, Any], revision: int) -> int: ...

    def set_event_callback(self, cb: Callable[[dict[str, Any]], None] | None) -> None: ...

    def snapshot_since(self, since_seq: int) -> tuple[int, list[dict[str, Any]]]: ...

    def write_point(self, device_key: str, point_id: int, value: str) -> int: ...

    def shutdown(self) -> None: ...
