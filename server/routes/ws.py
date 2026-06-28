from fastapi import APIRouter, Depends, Query, WebSocket, WebSocketDisconnect, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from database import get_db
from models import Device

router = APIRouter(tags=["websocket"])


class ConnectionManager:
    def __init__(self):
        self._connections: dict[str, set[WebSocket]] = {}

    def subscribe(self, device_id: str, ws: WebSocket):
        self._connections.setdefault(device_id, set()).add(ws)

    def unsubscribe(self, device_id: str, ws: WebSocket):
        subs = self._connections.get(device_id)
        if subs:
            subs.discard(ws)
            if not subs:
                del self._connections[device_id]

    def remove(self, ws: WebSocket):
        for device_id in list(self._connections.keys()):
            self.unsubscribe(device_id, ws)

    async def broadcast(self, device_id: str, message: dict):
        import json

        payload = json.dumps(message)
        dead = []
        for ws in self._connections.get(device_id, set()):
            try:
                await ws.send_text(payload)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.remove(ws)


manager = ConnectionManager()


@router.websocket("/ws")
async def ws_all_devices(
    ws: WebSocket,
):
    await ws.accept()
    try:
        while True:
            data = await ws.receive_json()
            action = data.get("action")
            device_id = data.get("device_id")
            if action == "subscribe" and device_id:
                manager.subscribe(device_id, ws)
            elif action == "unsubscribe" and device_id:
                manager.unsubscribe(device_id, ws)
    except WebSocketDisconnect:
        manager.remove(ws)
    except Exception:
        manager.remove(ws)


@router.websocket("/ws/{device_id}")
async def ws_device(
    device_id: str,
    ws: WebSocket,
):
    await ws.accept()
    manager.subscribe(device_id, ws)
    try:
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        manager.unsubscribe(device_id, ws)
    except Exception:
        manager.unsubscribe(device_id, ws)
