# 设备端固件 (firmware/)

> 版本：0.2.0 | 芯片：ESP32-C3-DevKitM-1 | 构建：PlatformIO + Arduino

## 架构

```
main.cpp (tick 调度器, ~70 行)
  ├── config.h          引脚/UUID/时间常量
  ├── utils.h           日志宏 + 设备 ID 生成
  ├── storage            NVS (Preferences) 读写封装
  ├── protocol           命令枚举 + JSON 解析/构建
  ├── scale_sensor       HX711 称重传感器
  ├── wifi_manager       WiFi STA 连接 + 指数退避重连
  ├── state_machine      7 状态机 + LED 指示
  ├── ble_server         BLE GATT 服务（7 个特征值）
  └── ble_protocol       特征值回调 + 命令分发
```

## loop() 调度

| 时机 | 操作 |
|------|------|
| 每 400ms | HX711 读取 (`scaleReadWeight(5)`) |
| 每 100ms | 状态机 tick + LED 刷新 |
| 持续 | WiFi 状态检查 + 重连 (`wifiManagerTick`) |
| 持续 | BLE 广告维护 (`bleServerTick`) |
| 每 2000ms | 串口心跳日志 |

**全程非阻塞**，无 `delay()`，无死循环。

## BLE GATT 服务

Service UUID: `F3860834-DD6C-4ED8-9555-3BE20F962A74`

| # | 特征值 | UUID suffix | Properties | 说明 |
|---|--------|-------------|------------|------|
| 1 | Device Info | `2A29F239-...` | READ | 设备 ID、固件版本、模式、状态 |
| 2 | WiFi Credentials | `C1B2A3D4-...` | WRITE | 写入 SSID + 密码配网 |
| 3 | Network Status | `D5E6F7A8-...` | READ, NOTIFY | WiFi/MQTT 连接状态 |
| 4 | Scale Settings | `B9C8D7E6-...` | READ, WRITE | 校准系数、单位、上传间隔 |
| 5 | Weight Stream | `F5E4D3C2-...` | NOTIFY | 实时重量 JSON (Phase 2) |
| 6 | Command | `A1B2C3D4-...` | WRITE | 命令请求 JSON |
| 7 | Event | `E8F7A6B5-...` | NOTIFY | 命令响应 + 异步事件 |

完整 UUID 见 `config.h`，规范为 `XXXXXXXX-DD6C-4ED8-9555-3BE20F962A74`。

### Device Info (READ)

```json
{"device_id":"esp32c3-a1b2c3","name":"ESPScale","firmware_version":"0.2.0","mode":"remote","state":"PROVISIONING"}
```

### WiFi Credentials (WRITE)

写入：
```json
{"ssid":"MyWiFi","password":"mypassword"}
```

### Network Status (READ/NOTIFY)

```json
{"wifi":{"connected":true,"rssi":-45,"ip":"192.168.1.100"},"mqtt":{"connected":false}}
```

## 命令协议

统一 JSON 格式，通过 Command 特征值 WRITE 触发，Event 特征值 NOTIFY 返回响应。

### Phase 1 支持的命令

| 命令 | 说明 | 请求示例 |
|------|------|----------|
| `get_status` | 返回完整设备状态 | `{"cmd":"get_status","params":{},"request_id":"xxx"}` |
| `reboot` | 重启设备 | `{"cmd":"reboot","params":{},"request_id":"xxx"}` |
| `factory_reset` | 清除 NVS 并重启 | `{"cmd":"factory_reset","params":{},"request_id":"xxx"}` |

### get_status 响应

```json
{"device_id":"esp32c3-a1b2c3","state":"RUNNING","wifi_connected":true,"hx711_ready":true,"cal_factor":397.6,"unit":"g","mode":"remote","heap_free":210000,"uptime_s":3600,"firmware_version":"0.2.0"}
```

### Phase 2 将支持的命令

`tare`, `calibrate`, `set_mode`, `set_config`, `set_wifi`, `set_mqtt`

## 状态机

```
PROVISIONING (无 WiFi 凭据)
     ↓ BLE 写入 WiFi Credentials
CONNECTING_WIFI
     ↓ 连接成功             ↓ 超时/失败
RUNNING                  ERROR_WIFI → 退避重试
     ↓ HX711 故障
ERROR_HX711

FACTORY_RESET → 清除 NVS → 重启
```

### LED 指示 (GPIO8, 低电平亮)

| 状态 | 模式 | 含义 |
|------|------|------|
| PROVISIONING | 200ms 亮 / 2000ms 灭 | 等待 BLE 配网 |
| CONNECTING_WIFI | 100ms 亮 / 500ms 灭 | WiFi 连接中 |
| RUNNING | 常亮 | 正常运行 |
| ERROR_WIFI | SOS 短 (200/200/200/1000) | WiFi 失败 |
| ERROR_HX711 | SOS 长 (500/500/500/2000) | 传感器故障 |

## 配网流程

1. 首次开机：无 NVS 凭据 → 进入 PROVISIONING 状态
2. 用 BLE 调试工具（nRF Connect / LightBlue）扫描 `ESPScale-XXXX`
3. 连接后向 WiFi Credentials 特征值写入 `{"ssid":"...","password":"..."}`  
4. 设备保存凭据到 NVS，开始连接 WiFi
5. 连接成功后进入 RUNNING 状态，LED 常亮
6. 断电重启后自动从 NVS 加载凭据并连接

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
| `device_id` | String | auto | 设备唯一 ID |
| `device_name` | String | `"ESPScale"` | 设备名 |
| `cal_factor` | Float | `397.6` | HX711 校准系数 |
| `unit` | String | `"g"` | 重量单位 |
| `upload_ms` | UInt | `5000` | 上传间隔 |
| `mode` | UChar | `0` | 0=remote, 1=local |
| `api_key` | String | auto | API 认证密钥 |
| `cfg_version` | UInt | `1` | 配置版本 |

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

### 依赖

```ini
h2zero/NimBLE-Arduino   # BLE 协议栈
bogde/HX711             # HX711 称重传感器
knolleary/PubSubClient  # MQTT (Phase 2)
bblanchon/ArduinoJson   # JSON 解析
```

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
[INFO] state=RUNNING wifi=1 ble=0 heap=205000
```

日志级别：`[INFO]` 正常、`[WARN]` 警告、`[ERROR]` 错误。
