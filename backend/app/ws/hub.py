from __future__ import annotations

import uuid
from collections import defaultdict
from typing import Any

from fastapi import WebSocket


class WsHub:
    """WebSocket 连接管理 + 多 topic 广播。

    支持四个主题：data / device / alarm / control。
    """

    TOPICS = ("data", "device", "alarm", "control")

    def __init__(self) -> None:
        self._clients: dict[str, WebSocket] = {}
        self._topics: dict[str, set[str]] = defaultdict(set)

    async def connect(self, ws: WebSocket) -> str:
        client_id = uuid.uuid4().hex
        self._clients[client_id] = ws
        return client_id

    async def disconnect(self, client_id: str) -> None:
        self._clients.pop(client_id, None)
        for topic_clients in self._topics.values():
            topic_clients.discard(client_id)

    def subscribe(self, client_id: str, topic: str) -> None:
        if topic in self.TOPICS:
            self._topics[topic].add(client_id)

    def unsubscribe(self, client_id: str, topic: str) -> None:
        self._topics[topic].discard(client_id)

    async def broadcast(self, topic: str, payload: dict[str, Any]) -> None:
        if topic not in self._topics:
            return
        dead: list[str] = []
        for cid in self._topics[topic]:
            ws = self._clients.get(cid)
            if ws is None:
                dead.append(cid)
                continue
            try:
                await ws.send_json(payload)
            except Exception:
                dead.append(cid)
        for cid in dead:
            await self.disconnect(cid)


ws_hub = WsHub()
