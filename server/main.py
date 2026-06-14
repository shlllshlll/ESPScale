import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from database import init_db
from mqtt_bridge import MqttBridge
from routes import data, devices, ws

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("server")

mqtt_bridge = MqttBridge()


@asynccontextmanager
async def lifespan(app: FastAPI):
    await init_db()
    logger.info("Database initialized")
    await mqtt_bridge.start()
    yield
    await mqtt_bridge.stop()


def create_app() -> FastAPI:
    app = FastAPI(
        title="ESPScale API",
        version="0.2.0",
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
