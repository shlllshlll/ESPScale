from fastapi import Depends, Header, HTTPException, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from auth import verify_api_key, verify_token
from config import APP_API_KEY
from database import get_db
from models import Device

security = HTTPBearer(auto_error=False)


async def get_current_user(
    credentials: HTTPAuthorizationCredentials | None = Depends(security),
) -> dict:
    if credentials is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing token")
    try:
        return verify_token(credentials.credentials)
    except ValueError:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid token") from None


async def get_app_auth(
    x_api_key: str | None = Header(default=None, alias="X-API-Key"),
) -> None:
    """Authenticate APP/client requests using a shared API key.
    If APP_API_KEY is empty, auth is disabled (dev mode)."""
    if not APP_API_KEY:
        return
    if x_api_key != APP_API_KEY:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid API key")


async def get_device_auth(
    x_device_id: str = Header(alias="X-Device-ID"),
    x_api_key: str = Header(alias="X-API-Key"),
    db: AsyncSession = Depends(get_db),
) -> Device:
    """Authenticate firmware/device requests using per-device API key."""
    if not x_device_id or not x_api_key:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing device auth")
    result = await db.execute(select(Device).where(Device.device_id == x_device_id))
    device = result.scalar_one_or_none()
    if device is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Unknown device")
    if not verify_api_key(x_api_key, device.api_key_hash):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid API key")
    return device