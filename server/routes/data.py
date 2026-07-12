import csv
import datetime
import io
import logging

from fastapi import APIRouter, Depends, Header, HTTPException, Query, status
from fastapi.responses import StreamingResponse
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from database import get_db
from deps import get_app_auth, get_device_auth
from models import Device, Event, WeightRecord
from schemas import WeightBatch, WeightData, WeightRecordResponse, WeightStats

router = APIRouter(tags=["data"])
logger = logging.getLogger("server.data")


@router.post("/api/v1/data", status_code=status.HTTP_201_CREATED)
async def ingest_weight(
    body: WeightData,
    device: Device = Depends(get_device_auth),
    db: AsyncSession = Depends(get_db),
):
    logger.info(
        "HTTP ingest device=%s weight=%.2f%s stable=%s seq=%s",
        device.device_id,
        body.weight,
        body.unit or "g",
        body.stable,
        body.seq,
    )
    ts = datetime.datetime.fromtimestamp(body.timestamp, tz=datetime.timezone.utc)
    record = WeightRecord(
        device_id=device.device_id,
        weight=body.weight,
        unit=body.unit,
        raw_value=body.raw,
        stable=body.stable,
        sequence_number=body.seq,
        timestamp=ts,
    )
    db.add(record)

    device.last_weight = body.weight
    device.last_seen = datetime.datetime.now(datetime.timezone.utc)
    device.is_online = True

    await db.commit()

    # Broadcast to WebSocket clients
    from routes.ws import manager

    await manager.broadcast(device.device_id, {
        "type": "weight_update",
        "device_id": device.device_id,
        "data": {
            "weight": body.weight,
            "unit": body.unit,
            "timestamp": body.timestamp,
            "seq": body.seq,
        },
    })
    return {"ok": True}


@router.post("/api/v1/data/batch", status_code=status.HTTP_201_CREATED)
async def ingest_weight_batch(
    body: WeightBatch,
    device: Device = Depends(get_device_auth),
    db: AsyncSession = Depends(get_db),
):
    now = datetime.datetime.now(datetime.timezone.utc)
    for item in body.records:
        ts = datetime.datetime.fromtimestamp(item.timestamp, tz=datetime.timezone.utc)
        record = WeightRecord(
            device_id=device.device_id,
            weight=item.weight,
            unit=item.unit,
            raw_value=item.raw,
            stable=item.stable,
            sequence_number=item.seq,
            timestamp=ts,
        )
        db.add(record)

    if body.records:
        last = body.records[-1]
        device.last_weight = last.weight
        device.last_seen = now
        device.is_online = True

    await db.commit()

    # Broadcast last record to WebSocket clients
    if body.records:
        from routes.ws import manager

        last = body.records[-1]
        await manager.broadcast(device.device_id, {
            "type": "weight_update",
            "device_id": device.device_id,
            "data": {
                "weight": last.weight,
                "unit": last.unit,
                "timestamp": last.timestamp,
                "seq": last.seq,
            },
        })

    return {"ok": True, "count": len(body.records)}


@router.get("/api/v1/devices/{device_id}/records", response_model=list[WeightRecordResponse])
async def get_records(
    device_id: str,
    db: AsyncSession = Depends(get_db),
    from_ts: int | None = Query(default=None, alias="from"),
    to_ts: int | None = Query(default=None, alias="to"),
    limit: int = Query(default=100, le=1000),
    offset: int = Query(default=0),
    _=Depends(get_app_auth),
):
    stmt = (
        select(WeightRecord)
        .where(WeightRecord.device_id == device_id)
        .order_by(WeightRecord.timestamp.desc())
        .limit(limit)
        .offset(offset)
    )
    if from_ts is not None:
        stmt = stmt.where(
            WeightRecord.timestamp >= datetime.datetime.fromtimestamp(from_ts, tz=datetime.timezone.utc)
        )
    if to_ts is not None:
        stmt = stmt.where(
            WeightRecord.timestamp <= datetime.datetime.fromtimestamp(to_ts, tz=datetime.timezone.utc)
        )

    result = await db.execute(stmt)
    return result.scalars().all()


@router.get("/api/v1/devices/{device_id}/records/latest", response_model=WeightRecordResponse | None)
async def get_latest_record(device_id: str, db: AsyncSession = Depends(get_db), _=Depends(get_app_auth)):
    result = await db.execute(
        select(WeightRecord)
        .where(WeightRecord.device_id == device_id)
        .order_by(WeightRecord.timestamp.desc())
        .limit(1)
    )
    return result.scalar_one_or_none()


@router.get("/api/v1/devices/{device_id}/records/stats", response_model=WeightStats)
async def get_stats(
    device_id: str,
    db: AsyncSession = Depends(get_db),
    from_ts: int | None = Query(default=None, alias="from"),
    to_ts: int | None = Query(default=None, alias="to"),
    _=Depends(get_app_auth),
):
    stmt = select(
        func.min(WeightRecord.weight),
        func.max(WeightRecord.weight),
        func.avg(WeightRecord.weight),
        func.count(WeightRecord.id),
    ).where(WeightRecord.device_id == device_id)

    if from_ts is not None:
        stmt = stmt.where(
            WeightRecord.timestamp >= datetime.datetime.fromtimestamp(from_ts, tz=datetime.timezone.utc)
        )
    if to_ts is not None:
        stmt = stmt.where(
            WeightRecord.timestamp <= datetime.datetime.fromtimestamp(to_ts, tz=datetime.timezone.utc)
        )

    result = await db.execute(stmt)
    row = result.one()
    return WeightStats(min=row[0], max=row[1], avg=round(row[2], 2) if row[2] else None, count=row[3])


@router.get("/api/v1/devices/{device_id}/records/export")
async def export_csv(
    device_id: str,
    db: AsyncSession = Depends(get_db),
    from_ts: int | None = Query(default=None, alias="from"),
    to_ts: int | None = Query(default=None, alias="to"),
    _=Depends(get_app_auth),
):
    stmt = (
        select(WeightRecord)
        .where(WeightRecord.device_id == device_id)
        .order_by(WeightRecord.timestamp.asc())
    )
    if from_ts is not None:
        stmt = stmt.where(
            WeightRecord.timestamp >= datetime.datetime.fromtimestamp(from_ts, tz=datetime.timezone.utc)
        )
    if to_ts is not None:
        stmt = stmt.where(
            WeightRecord.timestamp <= datetime.datetime.fromtimestamp(to_ts, tz=datetime.timezone.utc)
        )

    result = await db.execute(stmt)
    records = result.scalars().all()

    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerow(["timestamp", "weight", "unit", "stable", "sequence_number", "raw_value"])
    for r in records:
        writer.writerow([r.timestamp.isoformat(), r.weight, r.unit, r.stable, r.sequence_number, r.raw_value])

    output.seek(0)
    return StreamingResponse(
        iter([output.getvalue()]),
        media_type="text/csv",
        headers={"Content-Disposition": f"attachment; filename={device_id}_records.csv"},
    )
