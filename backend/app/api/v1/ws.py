from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from app.ws.hub import ws_hub

router = APIRouter()


@router.websocket("/ws/collect")
async def ws_collect(websocket: WebSocket) -> None:
    """WebSocket 主入口，支持四个 topic（data / device / alarm / control）。"""
    await websocket.accept()
    client_id = await ws_hub.connect(websocket)
    try:
        while True:
            msg = await websocket.receive_json()
            msg_type = msg.get("type")
            topic = msg.get("topic")
            if msg_type == "subscribe" and topic:
                ws_hub.subscribe(client_id, topic)
                await websocket.send_json({"type": "subscribed", "topic": topic})
            elif msg_type == "unsubscribe" and topic:
                ws_hub.unsubscribe(client_id, topic)
            elif msg_type == "ping":
                await websocket.send_json({"type": "pong"})
    except WebSocketDisconnect:
        pass
    finally:
        await ws_hub.disconnect(client_id)
