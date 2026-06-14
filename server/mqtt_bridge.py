import asyncio
import datetime
import json
import logging
import time

import paho.mqtt.client as mqtt
from sqlalchemy import select

from config import MQTT_BROKER_HOST, MQTT_BROKER_PORT
from database import async_session
from models import Device, Event, WeightRecord

logger = logging.getLogger("mqtt_bridge")

TOPIC_WEIGHT = "espscale/+/weight"
TOPIC_STATUS = "espscale/+/status"


def _device_id_from_topic(topic: str) -> str:
    parts = topic.split("/")
    return parts[1] if len(parts) >= 3 else ""


class MqttBridge:
    def __init__(self):
        self.client: mqtt.Client | None = None
        self._running = False

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        logger.info("MQTT connected (rc=%s)", reason_code)
        client.subscribe(TOPIC_WEIGHT, qos=1)
        client.subscribe(TOPIC_STATUS, qos=1)

    def _on_message(self, client, userdata, msg):
        topic = msg.topic
        device_id = _device_id_from_topic(topic)
        if not device_id:
            return

        try:
            payload = json.loads(msg.payload.decode())
        except json.JSONDecodeError:
            logger.warning("MQTT bad JSON: topic=%s", topic)
            return

        asyncio.ensure_future(self._handle_message(device_id, topic, payload))

    async def _handle_message(self, device_id: str, topic: str, payload: dict):
        from routes.ws import manager

        now = datetime.datetime.now(datetime.timezone.utc)

        async with async_session() as db:
            if topic.endswith("/weight"):
                weight = payload.get("weight", 0)
                ts = payload.get("timestamp", int(time.time()))
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

                result = await db.execute(select(Device).where(Device.device_id == device_id))
                device = result.scalar_one_or_none()
                if device:
                    device.last_weight = weight
                    device.last_seen = now
                    device.is_online = True

                await db.commit()

                await manager.broadcast(device_id, {
                    "type": "weight_update",
                    "device_id": device_id,
                    "data": {"weight": weight, "unit": payload.get("unit", "g"), "timestamp": ts, "seq": payload.get("seq")},
                })

            elif topic.endswith("/status"):
                status = payload.get("status", "")
                result = await db.execute(select(Device).where(Device.device_id == device_id))
                device = result.scalar_one_or_none()
                if device:
                    device.is_online = (status == "online")
                    if status == "online":
                        device.last_seen = now
                    await db.commit()

                event = Event(device_id=device_id, event_type=status, payload=json.dumps(payload))
                db.add(event)
                await db.commit()

                await manager.broadcast(device_id, {
                    "type": "device_status",
                    "device_id": device_id,
                    "data": payload,
                })

    async def start(self):
        self._running = True
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.connect_async(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60)
        self.client.socket().setblocking(False)
        logger.info("MQTT bridge started")

    async def stop(self):
        self._running = False
        if self.client:
            self.client.disconnect()
        logger.info("MQTT bridge stopped")
