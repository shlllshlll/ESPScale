# 设备端实施计划 (firmware/)

> **状态：Phase 1-2 ✅ 已实现** | 版本 v0.2.0

## 目标

将现有的单文件原型 `main.cpp` (238 行) 重构为模块化的物联网称重固件，支持：

1. **BLE 配网**：无需硬编码 WiFi 凭据，通过 BLE 发送并持久化到 NVS
2. **远程上传（模式 A/C）**：MQTT 定时上报 + HTTP POST 直传
3. **本地称重（模式 B）**：BLE 实时推流 + 命令控制（清零/校准/设置）

## 技术栈

| 项目 | 选型 | 状态 |
|------|------|------|
| 构建系统 | PlatformIO | ✅ |
| 框架 | Arduino (C++11) | ✅ |
| 芯片 | ESP32-C3-DevKitM-1 | ✅ |
| BLE | NimBLE-Arduino (h2zero) | ✅ |
| 称重 | HX711 (bogde) | ✅ |
| MQTT | PubSubClient (knolleary) | ⚠️ 基础功能有，缺自动连接/LWT/cmd |
| JSON | ArduinoJson (bblanchon) | ✅ |
| 存储 | ESP32 Preferences (NVS) | ✅ |

## 代码模块实现状态

```
firmware/src/
├── main.cpp              ✅ setup()/loop() 非阻塞调度 (108 行)
├── config.h              ✅ 引脚/UUID/常量/模式定义 (53 行)
├── utils.h               ✅ 日志宏 + deviceId 生成 (14 行)
├── storage.h/.cpp        ✅ NVS 持久化封装 (35+131 行)
├── state_machine.h/.cpp  ✅ 8 状态 FSM + LED (20+169 行)
├── ble_server.h/.cpp     ⚠️ NimBLE GATT (ScaleSettings 无回调) (15+122 行)
├── ble_protocol.h/.cpp   ✅ BLE 回调 + 9 命令分发 (12+301 行)
├── wifi_manager.h/.cpp   ✅ WiFi STA + 退避重试 (11+76 行)
├── mqtt_client.h/.cpp    ⚠️ MQTT 发布 (缺自动连接/LWT/cmd) (10+107 行)
├── http_client.h/.cpp    ✅ HTTP POST 状态机 (11+145 行)
├── scale_sensor.h/.cpp   ✅ HX711 读取/去皮/校准 (11+58 行)
└── protocol.h/.cpp       ✅ 命令协议解析 (28+67 行)
```

### 模块依赖关系 (已实现)

```
main.cpp
  ├── config.h               (无依赖)
  ├── state_machine          → config, storage
  ├── storage                → config
  ├── ble_server             → config
  │     └── ble_protocol     → config, protocol, storage, scale, wifi, mqtt, state
  ├── wifi_manager           → config, storage
  ├── mqtt_client            → config, storage, protocol
  ├── http_client            → config, storage, protocol
  ├── scale_sensor           → config, storage
  ├── protocol               → config
  └── utils                  → config
```

## BLE GATT 服务 (已实现)

Service UUID: `F3860834-DD6C-4ED8-9555-3BE20F962A74`

| # | 特征值 | UUID | Properties | 回调 | 状态 |
|---|--------|------|------------|------|------|
| 1 | Device Info | `2A29F239-...` | READ | ✅ bleOnDeviceInfoRead | ✅ |
| 2 | WiFi Credentials | `C1B2A3D4-...` | WRITE | ✅ bleOnWifiCredsWrite | ✅ |
| 3 | Network Status | `D5E6F7A8-...` | READ, NOTIFY | ✅ bleOnNetworkStatusRead | ✅ (MQTT 状态始终 false) |
| 4 | Scale Settings | `B9C8D7E6-...` | READ, WRITE | ❌ 无 | ❌ 需添加回调 |
| 5 | Weight Stream | `F5E4D3C2-...` | NOTIFY | — | ✅ |
| 6 | Command | `A1B2C3D4-...` | WRITE | ✅ bleOnCommandWrite | ✅ |
| 7 | Event | `E8F7A6B5-...` | NOTIFY | — | ✅ |

## 配网流程 (已实现)

```
首次开机 (无 NVS WiFi 凭据)
    │
    ▼
PROVISIONING 状态
    ├── 广播 "ESPScale-XXXX" + Service UUID
    ├── LED 慢闪 (200ms on / 2000ms off)
    │
    ├── [APP 通过 BLE 写入 WiFi Credentials]
    │     支持扩展字段: mode, server_url, mqtt_host/port
    │
    ▼
CONNECTING_WIFI 状态
    ├── 保存凭据到 NVS
    ├── WiFi.begin(ssid, pass)
    ├── LED 快闪 (100ms on / 600ms off)
    │
    ├── [成功] → CONNECTING_MQTT (MQTT模式) 或 RUNNING (HTTP/BLE模式)
    ├── [超时15s] → ERROR_WIFI → 重试3次 (线性退避) → 永久 ERROR
    │
    ▼
CONNECTING_MQTT 状态 (仅 MQTT 模式)
    ├── ⚠️ 当前不会自动连接 (Bug: 缺少 DISCONNECTED→CONNECTING 触发)
    ├── [成功] → RUNNING
    ├── [失败] → ERROR_MQTT → HTTP 回退
    │
    ▼
RUNNING 状态 (正常运行)
    ├── LED 常亮
    ├── 读取 HX711 每 400ms
    ├── HTTP_DIRECT 模式: HTTP POST 定时上传 (默认 5s 间隔)
    ├── MQTT 模式: MQTT 定时发布 (⚠️ 当前无法工作)
    ├── BLE_ONLY 模式: BLE 实时推流
    └── 响应 BLE 命令 (9 条)
```

## 状态机 (已实现)

```cpp
enum class DeviceState : uint8_t {
    PROVISIONING,       // ✅ 无存储的 WiFi 凭据, 等待 BLE 配网
    CONNECTING_WIFI,    // ✅ 正在连接 WiFi
    CONNECTING_MQTT,    // ✅ WiFi 已连接, 正在连接 MQTT
    RUNNING,            // ✅ 正常运行
    ERROR_WIFI,         // ✅ WiFi 连接失败 (3 次重试后永久错误)
    ERROR_MQTT,         // ✅ MQTT 不可用 (HTTP 回退中)
    ERROR_HX711,        // ✅ 称重传感器故障 (连续 10 次读取失败)
    FACTORY_RESET       // ⚠️ 枚举值存在但不可达 (实际通过 BLE 命令直接重启)
};
```

## LED 状态指示 (GPIO8, active LOW) — 已实现

| 状态 | 模式 | 含义 |
|------|------|------|
| PROVISIONING | 200ms on / 2000ms off | 等待 BLE 配网 |
| CONNECTING_WIFI | 100ms on / 600ms off | 连接 WiFi 中 |
| CONNECTING_MQTT | 100ms on / 600ms off | 连接 MQTT 中 |
| RUNNING | 常亮 | 正常运行 |
| ERROR_WIFI | 三连短闪 (1.6s cycle) | WiFi 失败 |
| ERROR_HX711 | 三连长闪 (3.5s cycle) | 传感器故障 |
| ERROR_MQTT / FACTORY_RESET | 灭 | — |

## NVS 存储 Schema — 已实现

Namespace: `espscale`

```
Key              Type      Default              描述
─────────────────────────────────────────────────────────
wifi_ssid        String    ""                   WiFi SSID
wifi_pass        String    ""                   WiFi 密码
mqtt_host        String    ""                   MQTT broker 地址
mqtt_port        UInt      1883                 MQTT 端口
mqtt_user        String    ""                   MQTT 用户名
mqtt_pass        String    ""                   MQTT 密码
device_id        String    auto (MAC-based)     设备唯一 ID (esp32c3-XXXXXX)
device_name      String    "ESPScale"           设备名称
cal_factor       Float     397.6               HX711 校准系数
unit             String    "g"                  重量单位
upload_ms        UInt      5000                 上传间隔 (ms)
mode             UChar     0                    0=HTTP_DIRECT, 1=MQTT, 2=BLE_ONLY
server_url       String    "http://espscale.shlll.top"  服务器地址
api_key          String    ""                   API 认证密钥
```

## 命令协议 — 已实现 (BLE)

统一 JSON 格式，通过 BLE Command 特征值写入触发。

### 已实现命令列表

| 命令 | 参数 | BLE | MQTT cmd | 说明 |
|------|------|-----|----------|------|
| `tare` | `{}` | ✅ | ❌ | 清零 (20 采样) |
| `calibrate` | `{"expected_weight":500.0}` | ✅ | ❌ | 用已知重量校准 |
| `set_mode` | `{"mode":0/1/2}` | ✅ | ❌ | 切换上传模式 |
| `set_config` | `{...}` | ✅ | ❌ | 修改配置 (mode/server_url/unit/interval/mqtt) |
| `set_wifi` | `{"ssid":"...","password":"..."}` | ✅ | ❌ | 更新 WiFi 并重连 |
| `set_mqtt` | `{"host":"...","port":1883,...}` | ✅ | ❌ | 更新 MQTT 配置 |
| `get_status` | `{}` | ✅ | ❌ | 返回完整设备状态 JSON |
| `reboot` | `{}` | ✅ | ❌ | ACK 后 ESP.restart() |
| `factory_reset` | `{}` | ✅ | ❌ | ACK 后清 NVS + restart |

## MQTT Topic 设计

```
espscale/{device_id}/weight    # 称重数据, QoS 1, 定时发布 ✅
espscale/{device_id}/status    # 设备状态/LWT, QoS 1, Retain ❌ 未实现
espscale/{device_id}/cmd       # 远程命令, QoS 1, 订阅 ❌ 未实现
```

### Weight Payload (已实现)
```json
{
  "device_id": "esp32c3-a1b2c3",
  "weight": 123.45,
  "unit": "g",
  "raw": 123456,
  "stable": true,
  "timestamp": 1749876543,
  "seq": 1042
}
```

## HTTP 上传 (已实现)

```
POST http://{server}/api/v1/data
Headers:
  Content-Type: application/json
  X-Device-ID: esp32c3-a1b2c3
  X-API-Key: {device_api_key}
Body: (weight JSON)
```

状态机: `IDLE → SENDING → WAITING → DONE → IDLE` (或 `ERROR → cooldown → IDLE`)

## main.cpp loop() 调度 (已实现)

| 间隔 | 任务 | 状态 |
|------|------|------|
| 400ms | HX711 读取 + BLE weight notify + 上传 (HTTP/MQTT) | ✅ |
| 100ms | 状态机 tick | ✅ |
| 每循环 | WiFi tick, HTTP tick, MQTT tick, BLE tick | ✅ |
| 2000ms | 串口心跳日志 | ✅ |

## 资源预算

| 组件 | 预计 RAM | 备注 |
|------|----------|------|
| NimBLE 协议栈 | ~40-60KB | 广播 + GATT 服务 |
| WiFi 协议栈 (STA) | ~50-70KB | TCP/IP + Wi-Fi 驱动 |
| Arduino 框架 | ~30KB | Core + Serial + GPIO |
| PubSubClient | ~10KB | MQTT 连接 + 缓冲区 |
| ArduinoJson | ~5-10KB | Stack-allocated JsonDocument |
| 应用代码 + NVS | ~20KB | 状态机/命令解析/存储 |
| **合计** | **~160-200KB** | 剩余 >200KB 安全 |

目标空闲堆: >50KB ✅

## 待修复项

### 🔴 严重: MQTT 客户端不会自动连接

**问题**: `mqtt_client` 状态机初始状态为 `DISCONNECTED`，但没有代码路径触发 `DISCONNECTED → CONNECTING` 转换。状态机模块的 `CONNECTING_MQTT` 状态期望 `mqttClientGetState()` 返回 `CONNECTED`，但 MQTT 客户端从未开始连接。

**修复方案**: 在 `mqttClientTick()` 中，当 WiFi 已连接且 mode 为 MQTT 时，自动从 `DISCONNECTED` 转为 `CONNECTING`。

### 🟡 中等: ScaleSettings BLE 特征值无回调

**问题**: `ble_server.cpp` 中创建了 ScaleSettings 特征值 (READ+WRITE)，但未设置任何 callback。

**修复方案**: 添加 `ScaleSettingsCallbacks`，onRead 返回当前 cal_factor/unit/upload_interval，onWrite 解析并保存。

### 🟡 中等: MQTT 缺少 LWT / status / cmd

**问题**: 
- connect 时未设置 LWT
- 未发布 `espscale/{id}/status` (online/offline)
- 未订阅 `espscale/{id}/cmd`

**修复方案**: 
1. `mqttClientBegin()` 中设置 LWT payload
2. 连接成功后发布 `{"status":"online"}` 到 status topic (Retain=true)
3. 订阅 cmd topic，在 `mqttCallback()` 中调用 `protocolParse()` 分发命令

### 🟢 低: 阻塞调用

- `scaleCalibrate()` 中 `delay(500)` — 仅 BLE 命令触发，不在主循环
- `scaleGetLastRaw()` 调用 `scale.read()` — 阻塞 ~100ms，在主循环 400ms 间隔中
