import os
from pathlib import Path

# Load secrets/server.env (or server/.env) before reading os.environ.
# Without this, `uvicorn main:app` has empty MQTT_USER/APP_API_KEY and the
# bridge/auth config silently diverges from firmware/APP secrets.
def _load_env_files() -> None:
    try:
        from dotenv import load_dotenv
    except ImportError:
        return
    root = Path(__file__).resolve().parent
    for path in (
        root / ".env",
        root.parent / "secrets" / "server.env",
    ):
        if path.is_file():
            load_dotenv(path, override=False)


_load_env_files()

DATABASE_URL: str = os.getenv("DATABASE_URL", "sqlite+aiosqlite:///espscale.db")
SECRET_KEY: str = os.getenv("SECRET_KEY", "dev-secret-change-in-production")
ACCESS_TOKEN_EXPIRE_MINUTES: int = int(os.getenv("ACCESS_TOKEN_EXPIRE_MINUTES", "10080"))

APP_API_KEY: str = os.getenv("APP_API_KEY", "")

# Embedded amqtt broker + bridge (no external Mosquitto)
MQTT_ENABLED: bool = os.getenv("MQTT_ENABLED", "true").lower() == "true"
MQTT_BIND: str = os.getenv("MQTT_BIND", "0.0.0.0")
MQTT_BROKER_HOST: str = os.getenv("MQTT_BROKER_HOST", "127.0.0.1")
MQTT_PORT: int = int(os.getenv("MQTT_PORT", os.getenv("MQTT_BROKER_PORT", "1883")))
MQTT_BROKER_PORT: int = MQTT_PORT  # alias for older code
MQTT_USER: str = os.getenv("MQTT_USER", "")
MQTT_PASS: str = os.getenv("MQTT_PASS", "")

DATA_RETENTION_ENABLED: bool = os.getenv("DATA_RETENTION_ENABLED", "true").lower() == "true"
DATA_RETENTION_DAYS: int = int(os.getenv("DATA_RETENTION_DAYS", "90"))
MAX_RECORDS_ENABLED: bool = os.getenv("MAX_RECORDS_ENABLED", "true").lower() == "true"
MAX_RECORDS_PER_DEVICE: int = int(os.getenv("MAX_RECORDS_PER_DEVICE", "100000"))
DB_VACUUM_ENABLED: bool = os.getenv("DB_VACUUM_ENABLED", "true").lower() == "true"
DB_VACUUM_INTERVAL_HOURS: int = int(os.getenv("DB_VACUUM_INTERVAL_HOURS", "24"))
DB_CLEANUP_INTERVAL_HOURS: int = int(os.getenv("DB_CLEANUP_INTERVAL_HOURS", "6"))
DB_WAL_MODE: bool = os.getenv("DB_WAL_MODE", "true").lower() == "true"
