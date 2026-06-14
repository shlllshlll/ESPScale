# CLAUDE.md — ESPScale 技术选型与开发规范

## 项目概述

ESPScale 是基于 ESP32-C3 + HX711 的物联网称重系统，采用 BLE + WiFi 双模通信，包含设备端固件、Python 服务端和 Flutter APP 三个模块。

## 技术栈

### 设备端 (firmware/)
| 项目 | 选型 | 原因 |
|------|------|------|
| 构建系统 | PlatformIO | 依赖管理方便，CI 友好 |
| 框架 | Arduino (C++11) | 现有代码基础，生态丰富 |
| 芯片 | ESP32-C3-DevKitM-1 | RISC-V, 400KB SRAM, 4MB Flash, BLE5+WiFi4 |
| BLE 协议栈 | NimBLE-Arduino (h2zero) | 比 Bluedroid 省 ~60% 内存，适合 C3 有限 SRAM |
| 称重传感器 | HX711 (bogde) | 已有，成熟稳定 |
| MQTT 客户端 | PubSubClient (knolleary) | 轻量、非阻塞 tick 模式 |
| JSON 解析 | ArduinoJson (bblanchon) | 嵌入式 JSON 事实标准 |
| 持久化存储 | ESP32 Preferences (NVS) | ESP32 内置，无需额外库 |
| WiFi | ESP32 Arduino WiFi (内置) | STA 模式 |

### 服务端 (server/)
| 项目 | 选型 | 原因 |
|------|------|------|
| 语言 | Python 3.11+ | 快速迭代，生态丰富 |
| Web 框架 | FastAPI | 异步原生支持，自动 OpenAPI 文档，WebSocket 内置 |
| ORM | SQLAlchemy 2.0 (async) | Python ORM 标准，async 支持成熟 |
| 数据库 | SQLite (aiosqlite) | 零配置部署，单文件，适合本项目规模 |
| 数据校验 | Pydantic v2 | FastAPI 内置，类型安全 |
| MQTT 客户端 | paho-mqtt | Python MQTT 标准库 |
| 认证 | python-jose (JWT) | 用户认证；设备用 API Key |
| 部署 | Docker Compose | Mosquitto + FastAPI 一键部署 |
| 迁移 | Alembic | SQLAlchemy 配套迁移工具 |

### APP 端 (app/)
| 项目 | 选型 | 原因 |
|------|------|------|
| 框架 | Flutter 3.x | 单代码库覆盖 iOS/Android/macOS/Windows/Linux |
| BLE | flutter_blue_plus | 跨平台 BLE 插件，最成熟 |
| 状态管理 | Riverpod | 类型安全，比 Provider 更灵活 |
| HTTP | dio | 拦截器、超时、重试 |
| 图表 | fl_chart | Flutter 最流行的图表库 |
| WebSocket | web_socket_channel | 官方推荐 |
| 代码生成 | freezed + json_serializable | 数据模型自动生成 |

## 开发规范

### 固件 (C++)

**命名规范:**
- 文件名: `snake_case.h` / `snake_case.cpp`
- 类名: `PascalCase` (如 `WifiManager`)
- 函数/变量: `camelCase` (如 `getWeight()`, `isConnected`)
- 常量: `UPPER_SNAKE_CASE` (如 `HX711_SCK_PIN`)
- 枚举值: `UPPER_SNAKE_CASE` (如 `DeviceState::PROVISIONING`)

**代码结构:**
- 每个 `.h/.cpp` 对只负责一个功能模块
- 头文件用 `#pragma once` 而非 include guards
- ArduinoJson 的 `JsonDocument` 统一用 `StaticJsonDocument<512>`（栈分配，避免堆碎片化）
- 所有串口输出用 `Serial.printf()` 或宏 `LOG_INFO()` / `LOG_ERROR()`
- loop() 中禁止任何阻塞调用 (no `delay()`, no `while(!condition)` without timeout)
- 使用 `millis()` 实现非阻塞定时
- MQTT 和 HTTP 操作在 loop() 的 tick 函数中分步执行

**内存管理:**
- 目标：始终保持 >50KB 空闲堆
- 避免动态 `new`/`delete`，优先栈分配
- BLE 特征值缓冲区复用（read/write 回调中用同一 `std::string`）
- JSON payload 限制在 512 字节以内

**日志规范:**
```cpp
#define LOG_INFO(fmt, ...)  Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
```

### 服务端 (Python)

**命名规范:**
- 遵循 PEP 8
- 文件名: `snake_case.py`
- 类名: `PascalCase`
- 函数/变量: `snake_case`

**代码结构:**
- FastAPI 路由文件放在 `routes/` 下，一个文件一组端点
- 数据库依赖通过 FastAPI `Depends()` 注入
- Pydantic schema 与 ORM model 分离（`schemas.py` vs `models.py`）
- MQTT bridge 在 FastAPI lifespan 中启动/停止

**格式化:** Black (line-length=100) + isort + ruff

### APP 端 (Flutter/Dart)

**命名规范:**
- 遵循 Effective Dart
- 文件名: `snake_case.dart`
- 类名: `PascalCase`
- 变量/函数: `camelCase`

**代码结构:**
- 按功能分层: `models/` → `services/` → `providers/` → `screens/` → `widgets/`
- BLE 操作在 `BleService` 中封装，对外暴露 Stream
- 每个页面一个 `ConsumerWidget` (Riverpod)
- 数据模型用 `freezed` 生成不可变类

## BLE UUID 规范

所有特征值 UUID 共用同一 group octets `DD6C-4ED8-9555-3BE20F962A74`：

```
SERVICE_UUID             = F3860834-DD6C-4ED8-9555-3BE20F962A74
CHAR_DEVICE_INFO_UUID    = 2A29F239-DD6C-4ED8-9555-3BE20F962A74
CHAR_WIFI_CREDS_UUID     = C1B2A3D4-DD6C-4ED8-9555-3BE20F962A74
CHAR_NETWORK_STATUS_UUID = D5E6F7A8-DD6C-4ED8-9555-3BE20F962A74
CHAR_SCALE_SETTINGS_UUID = B9C8D7E6-DD6C-4ED8-9555-3BE20F962A74
CHAR_WEIGHT_STREAM_UUID  = F5E4D3C2-DD6C-4ED8-9555-3BE20F962A74
CHAR_COMMAND_UUID        = A1B2C3D4-DD6C-4ED8-9555-3BE20F962A74
CHAR_EVENT_UUID          = E8F7A6B5-DD6C-4ED8-9555-3BE20F962A74
```

## MQTT Topic 规范

```
espscale/{device_id}/weight     # 称重数据 (QoS 1, Retain=false)
espscale/{device_id}/status     # 设备状态 + LWT (QoS 1, Retain=true)
espscale/{device_id}/cmd        # 远程命令 (QoS 1, Retain=false)
```

## 命令协议

所有命令（BLE + MQTT）共用 JSON 格式：

请求:
```json
{"cmd": "<command_name>", "params": {...}, "request_id": "<uuid-v4>"}
```

响应:
```json
{"event": "cmd_ack", "cmd": "<command_name>", "success": true, "request_id": "<uuid-v4>", "data": {...}}
```

## 开发命令

PlatformIO CLI 位于 `~/.platformio/penv/bin/pio`，配置 alias 或在固件目录下直接调用：

```bash
cd firmware && ~/.platformio/penv/bin/pio run              # 编译
cd firmware && ~/.platformio/penv/bin/pio run -t upload    # 编译并烧录
cd firmware && ~/.platformio/penv/bin/pio device monitor   # 串口监视 (115200)
```

## 资源约束

ESP32-C3:
- SRAM: ~400KB（可用 ~320KB after bootloader）
- Flash: 4MB（可用 ~3MB after partition table）
- 目标空闲堆: >50KB
- JSON 文档大小限制: 512 字节
- BLE MTU: 默认 256 字节

## 推荐开发顺序

1. firmware/ — 先完成 BLE 配网 + NVS 持久化
2. server/ — 数据接收 + 存储 + API
3. app/ — BLE 配网 + 数据显示
4. 集成测试 — 端到端验证
