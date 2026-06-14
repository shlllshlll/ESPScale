import asyncio
import sys

from bleak import BleakClient, BleakScanner

SERVICE_UUID = "F3860834-DD6C-4ED8-9555-3BE20F962A74"
CHAR_UUID = "1B796AC2-C6C4-4AC1-B586-7FB2318549D8"
DEVICE_NAME = "ESP32C3-BLE"


async def notification_handler(_, data):
    print("notify:", data.decode(errors="ignore"))


async def find_device(retries=3):
    for attempt in range(1, retries + 1):
        # First choice: match by advertised service UUID.
        device = await BleakScanner.find_device_by_filter(
            lambda _device, adv_data: SERVICE_UUID.lower() in {uuid.lower() for uuid in (adv_data.service_uuids or [])},
            timeout=5.0,
        )
        if device is not None:
            return device

        # Fallback on macOS: some scans omit service UUIDs, so match by name.
        device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=3.0)
        if device is not None:
            return device

        print(f"scan attempt {attempt}/{retries} did not find device")
        await asyncio.sleep(1)

    return None


async def main():
    command = (sys.argv[1] if len(sys.argv) > 1 else "OFF").strip().upper()
    if command not in {"ON", "OFF"}:
        raise ValueError("Command must be ON or OFF")

    device = await find_device()
    if device is None:
        raise RuntimeError("No BLE device advertising the target service UUID was found")

    print(f"connecting to: {device.name} ({device.address})")

    async with BleakClient(device) as client:
        await client.start_notify(CHAR_UUID, notification_handler)
        await client.write_gatt_char(CHAR_UUID, command.encode(), response=True)
        print(f"write: {command}")

        await asyncio.sleep(2)

        try:
            data = await client.read_gatt_char(CHAR_UUID)
            print("read:", data.decode(errors="ignore"))
        except Exception as exc:
            print(f"read failed: {exc}")

        await client.stop_notify(CHAR_UUID)


if __name__ == "__main__":
    asyncio.run(main())
