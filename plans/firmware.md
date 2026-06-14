# 设备端实施计划 (firmware/)

> 状态：Phase 1 待开始

## 目标

将现有的单文件原型 `main.cpp` (238 行) 重构为模块化的物联网称重固件，支持：

1. **BLE 配网**：无需硬编码 WiFi 凭据，通过 BLE 发送并持久化到 NVS
2. **远程上传（模式 A）**：MQTT 定时上报称重数据，HTTP POST 回退
3. **本地称重（模式 B）**：BLE 实时推流 + 命令控制（清零/校准/设置）

## 技术栈

| 项目 | 选型 |
|------|------|
| 构建系统 | PlatformIO |
| 框架 | Arduino (C++11) |
| 芯片 | ESP32-C3-DevKitM-1 |
| BLE | NimBLE-Arduino (h2zero) |
| 称重 | HX711 (bogde) |
| MQTT | PubSubClient (knolleary) |
| JSON | ArduinoJson (bblanchon) |
| 存储 | ESP32 Preferences (NVS) |

## 代码模块设计

```
firmware/src/
├── main.cpp              # setup(), loop() 调度 (约 80 行)
├── config.h              # 引脚/UUID/时间常量 (约 100 行)
├── state_machine.h/.cpp  # DeviceState 枚举, 状态转移, LED 模式
├── storage.h/.cpp        # Preferences 封装, 工厂默认值
├── ble_server.h/.cpp     # NimBLE 初始化/广告/特征值创建
├── ble_protocol.h/.cpp   # 特征值回调, JSON 解析/分发, notify
├── wifi_manager.h/.cpp   # WiFi STA 连接/状态/重连(指数退避)
├── mqtt_client.h/.cpp    # PubSubClient 非阻塞封装, LWT
├── http_client.h/.cpp    # HTTP POST 回退
├── scale_sensor.h/.cpp   # HX711 初始化/去皮/读取/校准
├── protocol.h/.cpp       # 命令枚举, JSON 请求/响应构建
└── utils.h/.cpp          # device ID生成, API key, 日志宏
```

### 模块依赖关系

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

## BLE GATT 服务

Service UUID: `F3860834-DD6C-4ED8-9555-3BE20F962A74`

| # | 特征值 | UUID | Properties | 方向 | 说明 |
|---|--------|------|------------|------|------|
| 1 | Device Info | `2A29F239-...` | READ | 设备→客户端 | 设备ID、固件版本、模式 |
| 2 | WiFi Credentials | `C1B2A3D4-...` | WRITE | 客户端→设备 | SSID + 密码 |
| 3 | Network Status | `D5E6F7A8-...` | READ, NOTIFY | 设备→客户端 | WiFi 和 MQTT 连接状态 |
| 4 | Scale Settings | `B9C8D7E6-...` | READ, WRITE | 双向 | 校准系数、上传间隔、单位 |
| 5 | Weight Stream | `F5E4D3C2-...` | NOTIFY | 设备→客户端 | 实时重量 JSON |
| 6 | Command | `A1B2C3D4-...` | WRITE | 客户端→设备 | 命令请求 |
| 7 | Event | `E8F7A6B5-...` | NOTIFY | 设备→客户端 | 命令响应 + 事件 |

## 配网流程

```
首次开机 (无 NVS WiFi 凭据)
    │
    ▼
PROVISIONING 状态
    ├── 广播 "ESPScale-XXXX" + Service UUID
    ├── LED 慢闪 (200ms on / 2000ms off)
    │
    ├── [APP 通过 BLE 写入 WiFi Credentials]
    │
    ▼
CONNECTING_WIFI 状态
    ├── 保存凭据到 NVS
    ├── WiFi.begin(ssid, pass)
    ├── LED 快闪 (100ms on / 500ms off)
    │
    ├── [成功] → CONNECTING_MQTT
    ├── [超时15s] → ERROR_WIFI → 重试3次 → 回 PROVISIONING
    │
    ▼
CONNECTING_MQTT 状态
    ├── 连接 MQTT broker
    ├── 设置 LWT: espscale/{id}/status (offline)
    ├── 发布 status: online
    │
    ├── [成功] → RUNNING
    ├── [超时10s] → ERROR_MQTT → 退避重试 + HTTP 回退
    │
    ▼
RUNNING 状态 (正常运行)
    ├── LED 常亮
    ├── 读取 HX711 每 400ms
    ├── remote 模式: MQTT 定时发布 (默认 5s 间隔)
    ├── local 模式: BLE 实时推流 (100ms)
    ├── MQTT 不可用时: HTTP POST 回退
    └── 响应 BLE/MQTT 命令
```

## 状态机

```
enum class DeviceState : uint8_t {
    PROVISIONING,       // 无存储的 WiFi 凭据, 等待 BLE 配网
    CONNECTING_WIFI,    // 正在连接 WiFi
    CONNECTING_MQTT,    // WiFi 已连接, 正在连接 MQTT
    RUNNING,            // 正常运行
    ERROR_WIFI,         // WiFi 连接失败 (退避重试中)
    ERROR_MQTT,         // MQTT 不可用 (HTTP 回退中)
    ERROR_HX711,        // 称重传感器故障
    FACTORY_RESET       // 清除 NVS 中，即将重启
};
```

## LED 状态指示 (GPIO8)

| 状态 | 模式 | 含义 |
|------|------|------|
| PROVISIONING | 200ms on / 2000ms off | 等待 BLE 配网 |
| CONNECTING_WIFI | 100ms on / 500ms off | 连接 WiFi 中 |
| CONNECTING_MQTT | 100ms on / 200ms off | 连接 MQTT 中 |
| RUNNING (MQTT OK) | 常亮 | 正常运行 |
| RUNNING (HTTP fallback) | 1000ms on / 100ms off | 降级模式 |
| ERROR_WIFI | SOS 短: 200/200/200/1000ms | WiFi 失败 |
| ERROR_HX711 | SOS 长: 500/500/500/2000ms | 传感器故障 |
| BLE 已连接 | 每 3s 快速双闪 (叠加) | 活跃 BLE 会话 |

## NVS 存储 Schema

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
mqtt_topic       String    "espscale"           MQTT topic 前缀
device_id        String    auto (MAC-based)     设备唯一 ID
device_name      String    "ESPScale"           设备名称
cal_factor       Float     397.6               HX711 校准系数
unit             String    "g"                  重量单位
upload_ms        UInt      5000                 上传间隔 (ms)
mode             UChar     0                    0=remote, 1=local
api_key          String    auto (32 hex)        API 认证密钥
cfg_version      UInt      1                    配置版本号
```

## 命令协议

统一 JSON 格式，BLE Command 特征值写入或 MQTT `cmd` topic 均可触发。

### 请求格式
```json
{"cmd": "<name>", "params": {...}, "request_id": "<uuid-v4>"}
```

### 响应格式
```json
{"event": "cmd_ack", "cmd": "<name>", "success": true, "data": {...}, "request_id": "<uuid-v4>"}
```

### 命令列表

| 命令 | 参数 | 说明 |
|------|------|------|
| `tare` | `{}` | 清零 (20 采样) |
| `calibrate` | `{"expected_weight":500.0}` | 用已知重量校准 |
| `set_mode` | `{"mode":"local"}` 或 `"remote"` | 切换数据上报模式 |
| `set_config` | `{"upload_interval_ms":10000}` | 修改部分配置 |
| `set_wifi` | `{"ssid":"...","password":"..."}` | 更新 WiFi 并重连 |
| `set_mqtt` | `{"host":"...","port":1883}` | 更新 MQTT 并重连 |
| `get_status` | `{}` | 返回完整设备状态 |
| `reboot` | `{}` | 重启设备 |
| `factory_reset` | `{}` | 清除所有 NVS 并重启 |

## MQTT Topic 设计

```
espscale/{device_id}/weight    # 称重数据, QoS 1, 定时发布
espscale/{device_id}/status    # 设备状态/LWT, QoS 1, Retain
espscale/{device_id}/cmd       # 远程命令, QoS 1, 订阅
```

### Weight Payload
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

### Status Payload (含 LWT)
```json
{
  "device_id": "esp32c3-a1b2c3",
  "status": "online",
  "wifi_rssi": -45,
  "heap_free": 98765,
  "uptime_s": 3600,
  "timestamp": 1749876543
}
```

LWT 在 MQTT connect 时设置：`{"device_id":"...","status":"offline","timestamp":0}`

## HTTP 回退

当 MQTT 不可用时:

```
POST http://{server}:8000/api/v1/data
Headers:
  Content-Type: application/json
  X-Device-ID: esp32c3-a1b2c3
  X-API-Key: {device_api_key}
Body: (weight JSON, 同上)
```

## main.cpp loop() 伪代码

```cpp
void loop() {
    unsigned long now = millis();

    // 1. 读称重传感器 (400ms)
    if (now - lastScaleRead >= 400) {
        scale_sensor_tick(now);
        lastScaleRead = now;
    }

    // 2. 状态机 tick (100ms)
    if (now - lastStateTick >= 100) {
        state_machine_tick(now);
        lastStateTick = now;
    }

    // 3. WiFi 管理 (重连检查等)
    wifi_manager_tick(now);

    // 4. MQTT 非阻塞 tick (reconnect + publish + keepalive)
    mqtt_client_tick(now);

    // 5. BLE 周期维护 (广告重开等)
    ble_server_tick(now);

    // 6. 心跳日志 (2000ms)
    if (now - lastSerialPrint >= 2000) {
        serial_heartbeat();
        lastSerialPrint = now;
    }
}
```

## 实施阶段

### Phase 1: BLE 配网 + 持久化 ✅ 待开始
1. 创建 `config.h`, `storage.h/.cpp`, `utils.h/.cpp`
2. 重构 BLE 服务（先 3 个特征值：Device Info, WiFi Creds, Network Status）
3. 实现 WiFi Manager（NVS 存储 + 指数退避重连）
4. 基础状态机（PROVISIONING → CONNECTING_WIFI → RUNNING）
5. 重写 `main.cpp` 为模块调度
6. 测试：配网 → 断电 → 自动重连 WiFi

### Phase 2: MQTT 上传 + 本地称重 ✅ 待开始
1. 添加 PubSubClient + ArduinoJson 依赖
2. 实现 MQTT Client（非阻塞, LWT, keepalive）
3. 实现 HTTP Client 回退
4. 完善 7 个 BLE 特征值
5. 实现全部 9 条命令
6. 完整状态机（ERROR_WIFI, ERROR_MQTT, ERROR_HX711）
7. 测试：MQTT 发布 + BLE 流 + HTTP 回退

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

目标空闲堆: >50KB
