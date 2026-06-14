import datetime

from sqlalchemy import Boolean, DateTime, Float, ForeignKey, Integer, String, Text, func
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column


class Base(DeclarativeBase):
    pass


class Device(Base):
    __tablename__ = "devices"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(String(64), unique=True, nullable=False, index=True)
    name: Mapped[str] = mapped_column(String(128), default="")
    api_key_hash: Mapped[str] = mapped_column(String(64), nullable=False)
    cal_factor: Mapped[float] = mapped_column(Float, default=397.6)
    unit: Mapped[str] = mapped_column(String(8), default="g")
    upload_interval_ms: Mapped[int] = mapped_column(Integer, default=5000)
    mode: Mapped[str] = mapped_column(String(16), default="remote")
    last_weight: Mapped[float | None] = mapped_column(Float, nullable=True)
    last_seen: Mapped[datetime.datetime | None] = mapped_column(DateTime, nullable=True)
    is_online: Mapped[bool] = mapped_column(Boolean, default=False)
    mqtt_broker: Mapped[str] = mapped_column(String(256), default="")
    wifi_ssid: Mapped[str] = mapped_column(String(64), default="")
    firmware_ver: Mapped[str] = mapped_column(String(16), default="")
    created_at: Mapped[datetime.datetime] = mapped_column(DateTime, server_default=func.now())
    updated_at: Mapped[datetime.datetime] = mapped_column(
        DateTime, server_default=func.now(), onupdate=func.now()
    )


class WeightRecord(Base):
    __tablename__ = "weight_records"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(
        String(64), ForeignKey("devices.device_id"), nullable=False, index=True
    )
    weight: Mapped[float] = mapped_column(Float, nullable=False)
    unit: Mapped[str] = mapped_column(String(8), default="g")
    raw_value: Mapped[int | None] = mapped_column(Integer, nullable=True)
    stable: Mapped[bool] = mapped_column(Boolean, default=True)
    sequence_number: Mapped[int | None] = mapped_column(Integer, nullable=True)
    timestamp: Mapped[datetime.datetime] = mapped_column(DateTime, nullable=False)
    received_at: Mapped[datetime.datetime] = mapped_column(DateTime, server_default=func.now())


class Event(Base):
    __tablename__ = "events"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(String(64), nullable=False, index=True)
    event_type: Mapped[str] = mapped_column(String(32), nullable=False)
    payload: Mapped[str | None] = mapped_column(Text, nullable=True)
    created_at: Mapped[datetime.datetime] = mapped_column(DateTime, server_default=func.now())
