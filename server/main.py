import asyncio
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from database import init_db
from mqtt_bridge import MqttBridge
from mqtt_broker import EmbeddedMqttBroker
from retention import retention_loop
from routes import data, devices, ws

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
    force=True,
)
# Ensure our loggers are visible under uvicorn's logging config
for _name in ("server", "server.data", "mqtt_bridge", "mqtt_broker"):
    logging.getLogger(_name).setLevel(logging.INFO)

logger = logging.getLogger("server")

mqtt_broker = EmbeddedMqttBroker()
mqtt_bridge = MqttBridge()


@asynccontextmanager
async def lifespan(app: FastAPI):
    from config import APP_API_KEY, MQTT_ENABLED, MQTT_PORT, MQTT_USER

    await init_db()
    logger.info(
        "Database initialized | MQTT_ENABLED=%s port=%s user=%r app_api_key=%s",
        MQTT_ENABLED,
        MQTT_PORT,
        MQTT_USER or "(anonymous)",
        "set" if APP_API_KEY else "EMPTY",
    )
    await mqtt_broker.start()
    await mqtt_bridge.start()
    retention_task = asyncio.create_task(retention_loop())
    yield
    retention_task.cancel()
    await mqtt_bridge.stop()
    await mqtt_broker.stop()


def create_app() -> FastAPI:
    app = FastAPI(
        title="ESPScale API",
        version="0.3.0",
        lifespan=lifespan,
    )
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        allow_credentials=True,
        allow_methods=["*"],
        allow_headers=["*"],
    )
    app.include_router(devices.router)
    app.include_router(data.router)
    app.include_router(ws.router)
    return app


app = create_app()
