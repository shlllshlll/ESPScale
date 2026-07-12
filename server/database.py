import asyncio
import os

from alembic import command
from alembic.config import Config
from sqlalchemy import event
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker, create_async_engine

from config import DATABASE_URL, DB_WAL_MODE

engine = create_async_engine(DATABASE_URL, echo=False)
async_session = async_sessionmaker(engine, class_=AsyncSession, expire_on_commit=False)


@event.listens_for(engine.sync_engine, "connect")
def _set_sqlite_pragma(dbapi_conn, connection_record):
    cursor = dbapi_conn.cursor()
    if DB_WAL_MODE:
        cursor.execute("PRAGMA journal_mode=WAL")
    cursor.execute("PRAGMA foreign_keys=ON")
    cursor.close()


async def get_db():
    async with async_session() as session:
        yield session


def _run_migrations() -> None:
    """Upgrade to head. If tables already exist from create_all era, stamp head."""
    cfg = Config(os.path.join(os.path.dirname(__file__), "alembic.ini"))
    try:
        command.upgrade(cfg, "head")
    except Exception as exc:
        # Legacy DB created via create_all: tables exist but no alembic_version
        msg = str(exc).lower()
        if "already exists" in msg or "duplicate" in msg:
            command.stamp(cfg, "head")
        else:
            raise


async def init_db():
    """Run Alembic migrations to head (replaces create_all)."""
    await asyncio.to_thread(_run_migrations)
