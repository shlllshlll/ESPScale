# 设备端固件 (firmware/)

> 版本：v0.2.0 | 芯片：ESP32-C3-DevKitM-1 | 构建：PlatformIO + Arduino

## 架构

```
main.cpp (tick 调度器, 108 行)
  ├── config.h                引脚/UUID/时间常量/模式常量
  ├── utils.h                 日志宏 + 设备 ID 生成
  ├── storage.h/.cpp          NVS (Preferences) 读写封装 + factory reset
  ├── protocol.h/.cpp         命令枚举 + JSON 解析/构建
  ├── scale_sensor.h/.cpp     HX711 称重传感器 (读取/去皮/校准)
  ├── wifi_manager.h/.cpp     WiFi STA 连接 + 退避重连
  ├── mqtt_client.h/.cpp      MQTT 非阻塞发布 (PubSubClient)
  ├── http_client.h/.cpp      HTTP POST 回退 (非阻塞状态机)
  ├── state_machine.h/.cpp    8 状态机 + LED 指示
  ├── ble_server.h/.cpp       NimBLE GATT 服务 (7 个特征值)
  └── ble_protocol.h/.cpp     特征值回调 + 9 条命令分发
```

## loop() 调度

| 时机 | 操作 |
|------|------|
| 每 400ms | HX711 读取 (`scaleReadWeight(5)`) + BLE Notify + HTTP/MQTT 上传 |
| 每 100ms | 状态机 tick + LED 刷新 |
| 持续 | WiFi 状态检查 + 重连 (`wifiManagerTick`) |
| 持续 | HTTP client 非阻塞状态机 (`httpClientTick`) |
| 持续 | MQTT client 非阻塞 (`mqttClientTick`) |
| 持续 | BLE 广告维护 (`bleServerTick`) |
| 每 2000ms | 串口心跳日志 |

**全程非阻塞**，无 `delay()`，无死循环。`loop()` 不在主循环中调用 `delay()`，BLE/MQTT/HTTP 都通过 tick 状态机分步执行。

## BLE GATT 服务

Service UUID: `F3860834-DD6C-4ED8-9555-3BE20F962A74`

| # | 特征值 | UUID suffix | Properties | 状态 | 说明 |
|---|--------|-------------|------------|------|------|
| 1 | Device Info | `2A29F239-...` | READ | ✅ | 设备 ID、固件版本、模式、状态 |
| 2 | WiFi Credentials | `C1B2A3D4-...` | WRITE | ✅ | 写入 SSID + 密码 + 扩展字段 (mode/server_url/mqtt) |
| 3 | Network Status | `D5E6F7A8-...` | READ, NOTIFY | ⚠️ MQTT 状态始终 false | WiFi 连接状态 |
| 4 | Scale Settings | `B9C8D7E6-...` | READ, WRITE | ❌ 回调未实现 | 校准系数、单位、上传间隔 |
| 5 | Weight Stream | `F5E4D3C2-...` | NOTIFY | ✅ | 实时重量 JSON |
| 6 | Command | `A1B2C3D4-...` | WRITE | ✅ | 命令请求 JSON |
| 7 | Event | `E8F7A6B5-...` | NOTIFY | ✅ | 命令响应 + 异步事件 |

完整 UUID 见 `config.h`，规范为 `XXXXXXXX-DD6C-4ED8-9555-3BE20F962A74`。

### Device Info (READ)

```json
{
  "device_id": "esp32c3-a1b2c3",
  "name": "ESPScale",
  "firmware_version": "0.2.0",
  "mode": 0,
  "mode_label": "http_direct",
  "state": "RUNNING"
}
```

`mode` 字段为整数：`0`=HTTP_DIRECT, `1`=MQTT, `2`=BLE_ONLY

### WiFi Credentials (WRITE)

```json
{
  "ssid": "MyWiFi",
  "password": "mypassword",
  "mode": 0,
  "server_url": "http://192.168.1.100:8000",
  "mqtt_host": "192.168.1.100",
  "mqtt_port": 1883
}
```

扩展字段 (mode/server_url/mqtt_*) 可选，会一并保存到 NVS。

### Network Status (READ/NOTIFY)

```json
{
  "wifi": {"connected": true, "rssi": -45, "ip": "192.168.1.100"},
  "mqtt": {"connected": false}
}
```

> ⚠️ 当前 MQTT 状态始终返回 false — MQTT 连接状态未集成进此特征值。

## 命令协议

统一 JSON 格式，通过 Command 特征值 WRITE 触发，Event 特征值 NOTIFY 返回响应。

请求：
```json
{"cmd": "tare", "params": {}, "request_id": "uuid-v4"}
```

响应：
```json
{"event": "cmd_ack", "cmd": "tare", "success": true, "request_id": "uuid-v4", "data": {}}
```

### 已实现的 9 条命令

| 命令 | 参数 | 说明 |
|------|------|------|
| `tare` | `{}` | 清零 (20 采样) |
| `calibrate` | `{"expected_weight": 500.0}` | 用已知重量校准 |
| `set_mode` | `{"mode": 0/1/2}` | 切换上传模式 (HTTP/MQTT/BLE) |
| `set_config` | `{...}` | 修改配置 (mode/server_url/mqtt/unit/interval/api_key) |
| `set_wifi` | `{"ssid":"...","password":"..."}` | 更新 WiFi 并重连 |
| `set_mqtt` | `{"host":"...","port":1883,...}` | 更新 MQTT 配置 |
| `get_status` | `{}` | 返回完整设备状态 (见下) |
| `reboot` | `{}` | 重启设备 |
| `factory_reset` | `{}` | 清除 NVS 并重启 |

### get_status 响应

```json
{
  "device_id": "esp32c3-a1b2c3",
  "state": "RUNNING",
  "wifi_connected": true,
  "hx711_ready": true,
  "cal_factor": 397.6,
  "unit": "g",
  "mode": 0,
  "server_url": "http://192.168.1.100:8000",
  "mqtt_host": "",
  "mqtt_port": 1883,
  "heap_free": 210000,
  "uptime_s": 3600,
  "firmware_version": "0.2.0"
}
```

## 状态机 (8 状态)

```
PROVISIONING (无 WiFi 凭据)
     ↓ BLE 写入 WiFi Credentials
CONNECTING_WIFI
     ↓ 连接成功             ↓ 超时/失败
CONNECTING_MQTT (仅 MQTT 模式)        ERROR_WIFI → 重试3次
     ↓ 连接成功             ↓ 失败
RUNNING                  ERROR_MQTT → HTTP 回退
     ↓ HX711 连续 10 次失败
ERROR_HX711

FACTORY_RESET (BLE 命令直接触发)
```

### LED 指示 (GPIO8, 低电平亮)

| 状态 | 模式 | 含义 |
|------|------|------|
| PROVISIONING | 200ms 亮 / 2000ms 灭 | 等待 BLE 配网 |
| CONNECTING_WIFI | 100ms 亮 / 600ms 灭 | WiFi 连接中 |
| CONNECTING_MQTT | 100ms 亮 / 600ms 灭 | MQTT 连接中 |
| RUNNING | 常亮 | 正常运行 |
| ERROR_WIFI | 三连短闪 (1.6s 周期) | WiFi 失败 |
| ERROR_HX711 | 三连长闪 (3.5s 周期) | 传感器故障 |
| ERROR_MQTT / FACTORY_RESET | 灭 | 不可达 / 主动重启前 |

## 配网流程

1. 首次开机：无 NVS 凭据 → 进入 PROVISIONING 状态
2. 用 APP 扫描 BLE → 发现 `ESPScale-XXXX`
3. 连接 → 读 Device Info → APP 显示设备信息
4. 输入 WiFi SSID/密码 + 选择模式 (HTTP/MQTT/BLE)
5. APP 写入 WiFi Credentials 特征值
6. 设备保存凭据到 NVS，开始连接 WiFi
7. 订阅 Network Status NOTIFY → 等待 wifi.connected=true
8. 连接成功后进入 RUNNING 状态，LED 常亮
9. APP 通过 set_config 命令下发 api_key → 设备存储供 HTTP 认证
10. 断电重启后自动从 NVS 加载凭据并连接

## NVS 存储

Namespace: `espscale`

| Key | Type | 默认值 | 说明 |
|-----|------|--------|------|
| `wifi_ssid` | String | `""` | WiFi SSID |
| `wifi_pass` | String | `""` | WiFi 密码 |
| `mqtt_host` | String | `""` | MQTT 地址 |
| `mqtt_port` | UShort | `1883` | MQTT 端口 |
| `mqtt_user` | String | `""` | MQTT 用户 |
| `mqtt_pass` | String | `""` | MQTT 密码 |
| `mqtt_topic` | String | `"espscale"` | Topic 前缀 |
| `server_url` | String | `http://espscale.shlll.top` | HTTP 服务器地址 |
| `device_id` | String | auto (MAC-based) | 设备唯一 ID (`esp32c3-XXXXXX`) |
| `device_name` | String | `"ESPScale"` | 设备名 |
| `cal_factor` | Float | `397.6` | HX711 校准系数 |
| `unit` | String | `"g"` | 重量单位 |
| `upload_ms` | UInt | `5000` | 上传间隔 (ms) |
| `mode` | UChar | `0` | 0=HTTP_DIRECT, 1=MQTT, 2=BLE_ONLY |
| `api_key` | String | `""` | API 认证密钥 (APP 端生成) |
| `cfg_version` | UInt | `1` | 配置版本 |

## 已知问题

- 🔴 MQTT 客户端状态机缺少 `DISCONNECTED → CONNECTING` 触发 — MQTT 模式下实际无法自动连接
- 🟡 ScaleSettings BLE 特征值 (READ/WRITE) 回调未实现
- 🟡 MQTT 未设置 LWT、未发布 status topic、未订阅 cmd topic
- 🟢 `scaleCalibrate()` 内部含 `delay(500)` — 仅 BLE 命令触发，不在主循环

## 构建与烧录

```bash
cd firmware

# 编译
~/.platformio/penv/bin/pio run

# 编译并烧录
~/.platformio/penv/bin/pio run -t upload

# 串口监视 (115200 baud)
~/.platformio/penv/bin/pio device monitor
```

### 依赖 (platformio.ini)

| 库 | 用途 |
|------|------|
| h2zero/NimBLE-Arduino | BLE 协议栈 (省 60% RAM vs Bluedroid) |
| bogde/HX711 | 称重传感器 |
| knolleary/PubSubClient | MQTT 客户端 |
| bblanchon/ArduinoJson | JSON 解析 |

## 串口日志格式

```
[INFO] Booting ESPScale 0.2.0...
[INFO] Storage loaded, device_id=esp32c3-a1b2c3
[INFO] HX711 ready, calFactor=397.60
[INFO] BLE server started: ESPScale-a1b2
[INFO] State: PROVISIONING -> CONNECTING_WIFI
[INFO] Setup done, free heap: 210000
[INFO] WiFi connecting to MyWiFi...
[INFO] WiFi connected, IP: 192.168.1.100
[INFO] State: CONNECTING_WIFI -> RUNNING
[INFO] state=RUNNING wifi=1 ble=0 mode=0 heap=205000
[INFO] HTTP POST OK (87 bytes resp)
[INFO] scale ready=1 raw=123456 weight=123.4g stable=1 send=0
```

日志级别：`[INFO]` 正常、`[WARN]` 警告、`[ERROR]` 错误。
