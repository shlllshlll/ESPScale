"""Embedded MQTT broker (amqtt) — no external Mosquitto required."""

from __future__ import annotations

import logging
import tempfile
from pathlib import Path

from amqtt.broker import Broker
from passlib.apps import custom_app_context as pwd_context

from config import MQTT_BIND, MQTT_ENABLED, MQTT_PASS, MQTT_PORT, MQTT_USER

logger = logging.getLogger("mqtt_broker")


class EmbeddedMqttBroker:
    def __init__(self) -> None:
        self._broker: Broker | None = None
        self._passwd_path: Path | None = None

    def _build_config(self) -> dict:
        bind = f"{MQTT_BIND}:{MQTT_PORT}"
        plugins: list = []

        if MQTT_USER and MQTT_PASS:
            self._passwd_path = Path(tempfile.gettempdir()) / "espscale_amqtt_passwd"
            hashed = pwd_context.hash(MQTT_PASS)
            self._passwd_path.write_text(f"{MQTT_USER}:{hashed}\n", encoding="utf-8")
            plugins.append(
                {
                    "amqtt.plugins.authentication.AnonymousAuthPlugin": {
                        "allow_anonymous": False,
                    }
                }
            )
            plugins.append(
                {
                    "amqtt.plugins.authentication.FileAuthPlugin": {
                        "password_file": str(self._passwd_path),
                    }
                }
            )
            logger.info("MQTT auth enabled (user=%s)", MQTT_USER)
        else:
            plugins.append(
                {
                    "amqtt.plugins.authentication.AnonymousAuthPlugin": {
                        "allow_anonymous": True,
                    }
                }
            )
            logger.info("MQTT auth disabled (anonymous allowed)")

        return {
            "listeners": {
                "default": {
                    "type": "tcp",
                    "bind": bind,
                }
            },
            "plugins": plugins,
        }

    async def start(self) -> None:
        if not MQTT_ENABLED:
            logger.info("Embedded MQTT broker disabled (MQTT_ENABLED=false)")
            return
        cfg = self._build_config()
        self._broker = Broker(cfg)
        await self._broker.start()
        logger.info("Embedded MQTT broker listening on %s:%s", MQTT_BIND, MQTT_PORT)

    async def stop(self) -> None:
        if self._broker is not None:
            await self._broker.shutdown()
            self._broker = None
            logger.info("Embedded MQTT broker stopped")
        if self._passwd_path and self._passwd_path.exists():
            try:
                self._passwd_path.unlink()
            except OSError:
                pass
