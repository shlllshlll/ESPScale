from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import delete, select
from sqlalchemy.ext.asyncio import AsyncSession

from auth import hash_api_key
from database import get_db
from deps import get_app_auth
from models import Device, Event, WeightRecord
from schemas import DeviceRegister, DeviceResponse, DeviceUpdate

router = APIRouter(prefix="/api/v1/devices", tags=["devices"])


@router.post("/register", response_model=DeviceResponse)
async def register_device(
    body: DeviceRegister,
    db: AsyncSession = Depends(get_db),
    _=Depends(get_app_auth),
):
    # Normalize to lowercase so esp32c3-15FE60 / esp32c3-15fe60 collapse.
    device_id = body.device_id.strip().lower()
    result = await db.execute(select(Device).where(Device.device_id == device_id))
    device = result.scalar_one_or_none()

    if device is None:
        # Also merge any legacy mixed-case row for the same physical device.
        result = await db.execute(select(Device).where(Device.device_id == body.device_id))
        device = result.scalar_one_or_none()
        if device is not None:
            device.device_id = device_id

    if device is not None:
        # Idempotent re-registration: update api_key hash so the device's
        # current key (which it sends in X-API-Key) is accepted.
        device.api_key_hash = hash_api_key(body.api_key)
        if body.name:
            device.name = body.name
        elif device.name == body.device_id or device.name.upper() == device_id.upper():
            device.name = device_id
        if body.firmware_ver:
            device.firmware_ver = body.firmware_ver
        await db.commit()
        await db.refresh(device)
        return device

    device = Device(
        device_id=device_id,
        name=(body.name or device_id),
        api_key_hash=hash_api_key(body.api_key),
        firmware_ver=body.firmware_ver,
        mode="http_direct",
    )
    db.add(device)
    await db.commit()
    await db.refresh(device)
    return device


@router.get("", response_model=list[DeviceResponse])
async def list_devices(
    db: AsyncSession = Depends(get_db),
    _=Depends(get_app_auth),
):
    result = await db.execute(select(Device).order_by(Device.updated_at.desc()))
    devices = list(result.scalars().all())
    # Collapse case-only duplicates (legacy uppercase MAC rows).
    seen: dict[str, Device] = {}
    for d in devices:
        key = d.device_id.lower()
        if key not in seen:
            seen[key] = d
    return list(seen.values())


@router.get("/{device_id}", response_model=DeviceResponse)
async def get_device(
    device_id: str,
    db: AsyncSession = Depends(get_db),
    _=Depends(get_app_auth),
):
    result = await db.execute(select(Device).where(Device.device_id == device_id))
    device = result.scalar_one_or_none()
    if device is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Device not found")
    return device


@router.put("/{device_id}", response_model=DeviceResponse)
async def update_device(
    device_id: str,
    body: DeviceUpdate,
    db: AsyncSession = Depends(get_db),
    _=Depends(get_app_auth),
):
    result = await db.execute(select(Device).where(Device.device_id == device_id))
    device = result.scalar_one_or_none()
    if device is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Device not found")

    update_data = body.model_dump(exclude_unset=True)
    for key, value in update_data.items():
        setattr(device, key, value)

    await db.commit()
    await db.refresh(device)
    return device


@router.delete("/{device_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_device(
    device_id: str,
    db: AsyncSession = Depends(get_db),
    _=Depends(get_app_auth),
):
    result = await db.execute(select(Device).where(Device.device_id == device_id))
    device = result.scalar_one_or_none()
    if device is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Device not found")

    await db.execute(delete(Event).where(Event.device_id == device_id))
    await db.execute(delete(WeightRecord).where(WeightRecord.device_id == device_id))
    await db.execute(delete(Device).where(Device.device_id == device_id))
    await db.commit()
