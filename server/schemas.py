import datetime
from typing import Optional

from pydantic import BaseModel, Field


class DeviceRegister(BaseModel):
    device_id: str = Field(..., max_length=64)
    api_key: str = Field(..., min_length=32, max_length=64)
    name: str = Field(default="", max_length=128)
    firmware_ver: str = Field(default="", max_length=16)


class DeviceUpdate(BaseModel):
    name: Optional[str] = Field(default=None, max_length=128)
    upload_interval_ms: Optional[int] = None
    unit: Optional[str] = None
    mode: Optional[str] = None
    server_url: Optional[str] = None
    mqtt_broker: Optional[str] = None


class DeviceResponse(BaseModel):
    device_id: str
    name: str
    cal_factor: float
    unit: str
    upload_interval_ms: int
    mode: str
    server_url: Optional[str] = None
    mqtt_broker: Optional[str] = None
    last_weight: Optional[float]
    last_seen: Optional[datetime.datetime]
    is_online: bool
    firmware_ver: str
    created_at: datetime.datetime
    updated_at: datetime.datetime

    model_config = {"from_attributes": True}


class WeightData(BaseModel):
    weight: float
    unit: str = "g"
    raw: Optional[int] = None
    stable: bool = True
    timestamp: int  # epoch seconds
    seq: Optional[int] = None


class WeightBatch(BaseModel):
    records: list[WeightData]


class WeightRecordResponse(BaseModel):
    id: int
    device_id: str
    weight: float
    unit: str
    raw_value: Optional[int]
    stable: bool
    sequence_number: Optional[int]
    timestamp: datetime.datetime
    received_at: datetime.datetime

    model_config = {"from_attributes": True}


class WeightStats(BaseModel):
    min: Optional[float]
    max: Optional[float]
    avg: Optional[float]
    count: int


class EventResponse(BaseModel):
    id: int
    device_id: str
    event_type: str
    payload: Optional[str]
    created_at: datetime.datetime

    model_config = {"from_attributes": True}
