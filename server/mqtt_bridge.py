"""MQTT client bridge: subscribe to device topics → DB + WebSocket."""

from __future__ import annotations

import asyncio
import datetime
import json
import logging
import time
from urllib.parse import quote

from amqtt.client import MQTTClient
from sqlalchemy import select

from config import MQTT_BROKER_HOST, MQTT_ENABLED, MQTT_PASS, MQTT_PORT, MQTT_USER
from database import async_session
from models import Device, Event, WeightRecord

logger = logging.getLogger("mqtt_bridge")

TOPIC_WEIGHT = "espscale/+/weight"
TOPIC_STATUS = "espscale/+/status"


def _device_id_from_topic(topic: str) -> str:
    parts = topic.split("/")
    return parts[1] if len(parts) >= 3 else ""


def _broker_uri() -> str:
    host = MQTT_BROKER_HOST
    port = MQTT_PORT
    if MQTT_USER:
        user = quote(MQTT_USER, safe="")
        password = quote(MQTT_PASS or "", safe="")
        return f"mqtt://{user}:{password}@{host}:{port}"
    return f"mqtt://{host}:{port}"


class MqttBridge:
    def __init__(self) -> None:
        self.client: MQTTClient | None = None
        self._running = False
        self._task: asyncio.Task | None = None

    async def _handle_message(self, device_id: str, topic: str, payload: dict) -> None:
        from routes.ws import manager

        now = datetime.datetime.now(datetime.timezone.utc)

        async with async_session() as db:
            result = await db.execute(select(Device).where(Device.device_id == device_id))
            device = result.scalar_one_or_none()
            if device is None:
                logger.warning("MQTT message from unregistered device %s — ignored", device_id)
                return

            if topic.endswith("/weight"):
                weight = payload.get("weight", 0)
                ts = payload.get("timestamp", int(time.time()))
                logger.info(
                    "MQTT ingest device=%s weight=%.2f%s topic=%s",
                    device_id,
                    float(weight or 0),
                    payload.get("unit", "g"),
                    topic,
                )
                record_time = datetime.datetime.fromtimestamp(ts, tz=datetime.timezone.utc)
                record = WeightRecord(
                    device_id=device_id,
                    weight=weight,
                    unit=payload.get("unit", "g"),
                    raw_value=payload.get("raw"),
                    stable=payload.get("stable", True),
                    sequence_number=payload.get("seq"),
                    timestamp=record_time,
                )
                db.add(record)

                device.last_weight = weight
                device.last_seen = now
                device.is_online = True

                await db.commit()

                await manager.broadcast(
                    device_id,
                    {
                        "type": "weight_update",
                        "device_id": device_id,
                        "data": {
                            "weight": weight,
                            "unit": payload.get("unit", "g"),
                            "timestamp": ts,
                            "seq": payload.get("seq"),
                        },
                    },
                )

            elif topic.endswith("/status"):
                status_val = payload.get("status", "")
                device.is_online = status_val == "online"
                if status_val == "online":
                    device.last_seen = now

                event = Event(
                    device_id=device_id, event_type=status_val, payload=json.dumps(payload)
                )
                db.add(event)
                await db.commit()

                await manager.broadcast(
                    device_id,
                    {
                        "type": "device_status",
                        "device_id": device_id,
                        "data": payload,
                    },
                )

    async def _listen_loop(self) -> None:
        assert self.client is not None
        while self._running:
            try:
                message = await self.client.deliver_message()
                if message is None:
                    continue
                topic = message.topic or ""
                raw = message.data or b""
                if isinstance(raw, str):
                    raw = raw.encode()
                device_id = _device_id_from_topic(topic)
                logger.info("MQTT rx topic=%s device=%s bytes=%d", topic, device_id, len(raw))
                if not device_id:
                    continue
                try:
                    payload = json.loads(raw.decode())
                except (json.JSONDecodeError, UnicodeDecodeError):
                    logger.warning("MQTT bad JSON: topic=%s raw=%r", topic, raw[:200])
                    continue
                await self._handle_message(device_id, topic, payload)
            except asyncio.CancelledError:
                raise
            except Exception:
                logger.exception("MQTT listen error")
                await asyncio.sleep(1)

    async def start(self) -> None:
        if not MQTT_ENABLED:
            logger.info("MQTT bridge disabled (MQTT_ENABLED=false)")
            return

        self._running = True
        uri = _broker_uri()
        self.client = MQTTClient()
        # Brief delay so embedded broker is fully accepting connections
        await asyncio.sleep(0.2)
        await self.client.connect(uri)
        await self.client.subscribe([(TOPIC_WEIGHT, 1), (TOPIC_STATUS, 1)])
        self._task = asyncio.create_task(self._listen_loop())
        logger.info("MQTT bridge connected to %s:%s", MQTT_BROKER_HOST, MQTT_PORT)

    async def stop(self) -> None:
        self._running = False
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            self._task = None
        if self.client:
            try:
                await self.client.disconnect()
            except Exception:
                pass
            self.client = None
        logger.info("MQTT bridge stopped")
