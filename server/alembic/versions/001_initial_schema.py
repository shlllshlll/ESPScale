"""initial schema

Revision ID: 001
Revises:
Create Date: 2026-07-12

"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "001"
down_revision: Union[str, Sequence[str], None] = None
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.create_table(
        "devices",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("device_id", sa.String(length=64), nullable=False),
        sa.Column("name", sa.String(length=128), nullable=False),
        sa.Column("api_key_hash", sa.String(length=64), nullable=False),
        sa.Column("cal_factor", sa.Float(), nullable=False),
        sa.Column("unit", sa.String(length=8), nullable=False),
        sa.Column("upload_interval_ms", sa.Integer(), nullable=False),
        sa.Column("mode", sa.String(length=16), nullable=False),
        sa.Column("last_weight", sa.Float(), nullable=True),
        sa.Column("last_seen", sa.DateTime(), nullable=True),
        sa.Column("is_online", sa.Boolean(), nullable=False),
        sa.Column("server_url", sa.String(length=256), nullable=False),
        sa.Column("mqtt_broker", sa.String(length=256), nullable=False),
        sa.Column("wifi_ssid", sa.String(length=64), nullable=False),
        sa.Column("firmware_ver", sa.String(length=16), nullable=False),
        sa.Column("created_at", sa.DateTime(), server_default=sa.text("(CURRENT_TIMESTAMP)"), nullable=False),
        sa.Column("updated_at", sa.DateTime(), server_default=sa.text("(CURRENT_TIMESTAMP)"), nullable=False),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("device_id"),
    )
    with op.batch_alter_table("devices", schema=None) as batch_op:
        batch_op.create_index(batch_op.f("ix_devices_device_id"), ["device_id"], unique=True)

    op.create_table(
        "events",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("device_id", sa.String(length=64), nullable=False),
        sa.Column("event_type", sa.String(length=32), nullable=False),
        sa.Column("payload", sa.Text(), nullable=True),
        sa.Column("created_at", sa.DateTime(), server_default=sa.text("(CURRENT_TIMESTAMP)"), nullable=False),
        sa.PrimaryKeyConstraint("id"),
    )
    with op.batch_alter_table("events", schema=None) as batch_op:
        batch_op.create_index(batch_op.f("ix_events_device_id"), ["device_id"], unique=False)

    op.create_table(
        "weight_records",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("device_id", sa.String(length=64), nullable=False),
        sa.Column("weight", sa.Float(), nullable=False),
        sa.Column("unit", sa.String(length=8), nullable=False),
        sa.Column("raw_value", sa.Integer(), nullable=True),
        sa.Column("stable", sa.Boolean(), nullable=False),
        sa.Column("sequence_number", sa.Integer(), nullable=True),
        sa.Column("timestamp", sa.DateTime(), nullable=False),
        sa.Column("received_at", sa.DateTime(), server_default=sa.text("(CURRENT_TIMESTAMP)"), nullable=False),
        sa.ForeignKeyConstraint(["device_id"], ["devices.device_id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
    )
    with op.batch_alter_table("weight_records", schema=None) as batch_op:
        batch_op.create_index(batch_op.f("ix_weight_records_device_id"), ["device_id"], unique=False)


def downgrade() -> None:
    with op.batch_alter_table("weight_records", schema=None) as batch_op:
        batch_op.drop_index(batch_op.f("ix_weight_records_device_id"))
    op.drop_table("weight_records")
    with op.batch_alter_table("events", schema=None) as batch_op:
        batch_op.drop_index(batch_op.f("ix_events_device_id"))
    op.drop_table("events")
    with op.batch_alter_table("devices", schema=None) as batch_op:
        batch_op.drop_index(batch_op.f("ix_devices_device_id"))
    op.drop_table("devices")
