from __future__ import annotations

import asyncio
from collections import deque
from typing import Any


def command_key(device_code: str, point_id: int) -> tuple[str, int]:
    return (str(device_code), int(point_id))


class CommandWaiter:
    """控制命令同步等待（01 号 §7.4 wait=true）。

    扩展的 write_point 可能在同一线程内立刻回调 write_result，因此 Future
    必须在调用 write_point 之前注册；键用 (device_code, point_id)。
    """

    def __init__(self) -> None:
        self._queues: dict[tuple[str, int], deque[asyncio.Future]] = {}

    def register(self, key: tuple[str, int]) -> asyncio.Future:
        fut = asyncio.get_running_loop().create_future()
        self._queues.setdefault(key, deque()).append(fut)
        return fut

    def resolve(self, key: tuple[str, int], payload: dict[str, Any]) -> bool:
        queue = self._queues.get(key)
        while queue:
            fut = queue.popleft()
            if not fut.done():
                fut.set_result(payload)
                if not queue:
                    self._queues.pop(key, None)
                return True
        if queue is not None and not queue:
            self._queues.pop(key, None)
        return False

    def discard(self, key: tuple[str, int], fut: asyncio.Future | None) -> None:
        if fut is None:
            return
        queue = self._queues.get(key)
        if not queue:
            return
        try:
            queue.remove(fut)
        except ValueError:
            pass
        if not queue:
            self._queues.pop(key, None)


command_waiter = CommandWaiter()
