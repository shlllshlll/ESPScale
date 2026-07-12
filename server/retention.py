import asyncio
import logging
from datetime import datetime, timedelta, timezone

from sqlalchemy import delete, func, select

from config import (
    DB_CLEANUP_INTERVAL_HOURS,
    DB_VACUUM_ENABLED,
    DB_VACUUM_INTERVAL_HOURS,
    DATA_RETENTION_DAYS,
    DATA_RETENTION_ENABLED,
    MAX_RECORDS_ENABLED,
    MAX_RECORDS_PER_DEVICE,
)
from database import async_session, engine
from models import Device, Event, WeightRecord

logger = logging.getLogger("retention")


async def cleanup_old_records():
    if not DATA_RETENTION_ENABLED:
        return
    cutoff = datetime.now(timezone.utc) - timedelta(days=DATA_RETENTION_DAYS)
    async with async_session() as db:
        result = await db.execute(
            delete(WeightRecord).where(WeightRecord.timestamp < cutoff)
        )
        await db.execute(
            delete(Event).where(Event.created_at < cutoff)
        )
        await db.commit()
        if result.rowcount:
            logger.info("Cleaned up %d weight records older than %d days", result.rowcount, DATA_RETENTION_DAYS)


async def cleanup_excess_records():
    if not MAX_RECORDS_ENABLED:
        return
    async with async_session() as db:
        result = await db.execute(select(Device.device_id))
        device_ids = [row[0] for row in result.all()]

        for device_id in device_ids:
            count_result = await db.execute(
                select(func.count(WeightRecord.id)).where(WeightRecord.device_id == device_id)
            )
            total = count_result.scalar() or 0
            if total <= MAX_RECORDS_PER_DEVICE:
                continue

            excess = total - MAX_RECORDS_PER_DEVICE
            subq = (
                select(WeightRecord.id)
                .where(WeightRecord.device_id == device_id)
                .order_by(WeightRecord.timestamp.asc())
                .limit(excess)
            )
            await db.execute(delete(WeightRecord).where(WeightRecord.id.in_(subq)))
            logger.info("Trimmed %d excess records for device %s", excess, device_id)

        await db.commit()


async def vacuum_database():
    if not DB_VACUUM_ENABLED:
        return
    async with engine.begin() as conn:
        await conn.exec_driver_sql("VACUUM")
    logger.info("Database VACUUM complete")


async def retention_loop():
    logger.info(
        "Retention loop started (cleanup every %dh, vacuum every %dh, retention=%d days, max=%d/device)",
        DB_CLEANUP_INTERVAL_HOURS,
        DB_VACUUM_INTERVAL_HOURS,
        DATA_RETENTION_DAYS if DATA_RETENTION_ENABLED else 0,
        MAX_RECORDS_PER_DEVICE if MAX_RECORDS_ENABLED else 0,
    )
    cleanup_interval = DB_CLEANUP_INTERVAL_HOURS * 3600
    vacuum_interval = DB_VACUUM_INTERVAL_HOURS * 3600
    last_vacuum = asyncio.get_event_loop().time()

    while True:
        try:
            await cleanup_old_records()
            await cleanup_excess_records()
        except Exception:
            logger.exception("Retention cleanup failed")

        now = asyncio.get_event_loop().time()
        if DB_VACUUM_ENABLED and (now - last_vacuum) >= vacuum_interval:
            try:
                await vacuum_database()
                last_vacuum = now
            except Exception:
                logger.exception("VACUUM failed")

        await asyncio.sleep(cleanup_interval)