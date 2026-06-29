# CLAUDE.md — ESPScale 技术选型与开发规范

## 项目概述

ESPScale 是基于 ESP32-C3 + HX711 的物联网称重系统，采用 BLE + WiFi 双模通信，包含设备端固件、Python 服务端和 Flutter APP 三个模块。

> **当前版本**: v0.2.0 — 三端核心功能已实现，详见各模块实施状态。

## 实施状态总览

| 模块 | Phase | 状态 | 核心完成度 |
|------|-------|------|-----------|
| firmware/ | Phase 1-2 | ✅ 已实现 | BLE 配网 + NVS + MQTT + HTTP + 9 命令 + 状态机 |
| server/ | Phase 3 | ✅ 已实现 | REST API + WebSocket + MQTT Bridge + Docker |
| app/ | Phase 4 | ✅ 已实现 | BLE 配网 + 实时称重 + 图表 + 设备管理 |

### 待修复 / 待完成项

**固件端 (高优先级)**:
- 🔴 MQTT 客户端缺少 `DISCONNECTED → CONNECTING` 触发，需修复状态机
- 🟡 ScaleSettings BLE 特征值缺少 read/write 回调
- 🟡 MQTT 未设置 LWT、未发布 status topic、未订阅 cmd topic

**APP 端 (中优先级)**:
- 🟡 校准页面 "去皮" 按钮误调用 calibrate
- 🟡 "清除数据" 按钮 (`settings_screen.dart`) 空实现
- 🟢 生产代码中残留多个 `print()` 调试语句

**服务端 (中优先级)**:
- 🟡 缺少用户登录/注册端点（JWT 工具已有但无签发入口）
- 🟡 大部分读端点无认证保护
- 🟢 Alembic 迁移未配置（当前使用 `create_all`）
- 🟢 `EventResponse` schema 已定义但无对应端点

### v0.2.0 修复记录 (本次迭代)

| 修复 | 影响 |
|------|------|
| APP 路由 `/settings` → `/device-settings` | 设置按钮可正确进入 |
| `DropdownButtonFormField` 改用 `initialValue` | 适配 Flutter 3.44 |
| APP 配网用真实 32 hex API Key 下发固件 NVS | HTTP 模式 401 → 201 |
| 服务端 `register` 端点幂等更新 api_key | 重复配网不会 409 |
| APP 配网前预读设备配置 (`get_status`) | 预填 server_url/mqtt_host/port |
| APP 持久化 WiFi 凭证 | 下次配网自动回填 |
| 实时重量 BLE 优先，WS 仅作后备 | 远程模式仍可低延迟本地显示 |
| `set_config` 命令支持 `api_key` 参数 | 固件 NVS 写入 API 认证密钥 |

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
| MQTT 客户端 | paho-mqtt v2 | Python MQTT 标准库，v2 API (CallbackAPIVersion.VERSION2) |
| 认证 | python-jose (JWT) | 用户认证；设备用 API Key (SHA256) |
| 部署 | Docker Compose | Mosquitto + FastAPI 一键部署 |
| 迁移 | Alembic | SQLAlchemy 配套迁移工具 (⚠️ 尚未配置) |

### APP 端 (app/)
| 项目 | 选型 | 原因 | 实际使用 |
|------|------|------|----------|
| 框架 | Flutter 3.x | 单代码库覆盖 iOS/Android/macOS/Windows/Linux | ✅ |
| BLE | flutter_blue_plus | 跨平台 BLE 插件，最成熟 | ✅ |
| 状态管理 | Riverpod (v3) | 类型安全，比 Provider 更灵活 | ✅ AsyncNotifier/StreamProvider/Notifier |
| HTTP | dio | 拦截器、超时、重试 | ✅ |
| 图表 | fl_chart | Flutter 最流行的图表库 | ✅ |
| WebSocket | web_socket_channel | 官方推荐 | ✅ |
| 路由 | Navigator + named routes | 简单场景够用 | ✅ (未用 go_router) |
| 本地存储 | shared_preferences | 持久化服务器地址 | ✅ |
| 代码生成 | freezed + json_serializable | 数据模型自动生成 | ❌ 实际使用手写序列化 |

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

**固件源文件清单:**

| 文件 | 职责 | 状态 |
|------|------|------|
| `config.h` | 引脚/UUID/常量/模式定义 | ✅ |
| `utils.h` | 日志宏 + deviceId 生成 | ✅ |
| `storage.h/.cpp` | NVS 持久化 (Preferences) | ✅ |
| `scale_sensor.h/.cpp` | HX711 读取/去皮/校准 | ✅ |
| `wifi_manager.h/.cpp` | WiFi STA 连接 + 退避重试 | ✅ |
| `state_machine.h/.cpp` | 8 状态 FSM + LED 指示 | ✅ |
| `ble_server.h/.cpp` | NimBLE GATT 服务初始化 | ⚠️ ScaleSettings 无回调 |
| `ble_protocol.h/.cpp` | BLE 回调 + 命令分发 (9 条) | ✅ |
| `protocol.h/.cpp` | 统一命令协议解析 | ✅ |
| `mqtt_client.h/.cpp` | MQTT 非阻塞发布 | ⚠️ 缺少自动连接/LWT/cmd |
| `http_client.h/.cpp` | HTTP POST 状态机 | ✅ |
| `main.cpp` | setup()/loop() 调度 | ✅ |

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

**服务端源文件清单:**

| 文件 | 职责 | 状态 |
|------|------|------|
| `main.py` | FastAPI app + lifespan + CORS | ✅ |
| `config.py` | 环境变量配置 | ✅ |
| `database.py` | SQLAlchemy async engine + session | ✅ |
| `models.py` | Device, WeightRecord, Event | ✅ |
| `schemas.py` | Pydantic v2 request/response | ✅ |
| `auth.py` | JWT 签发/验证 + API Key 哈希 | ✅ |
| `deps.py` | FastAPI 依赖注入 | ✅ (部分未使用) |
| `routes/devices.py` | 设备注册 + CRUD (5 端点) | ✅ |
| `routes/data.py` | 数据摄入 + 查询 + 统计 + CSV (7 端点) | ✅ |
| `routes/ws.py` | WebSocket 实时推送 (2 端点) | ✅ |
| `mqtt_bridge.py` | MQTT → DB → WS 桥接 | ✅ |

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
- 数据模型手写 `fromJson`/`toJson`（未使用 freezed）

**APP 端源文件清单:**

| 文件 | 职责 | 状态 |
|------|------|------|
| `main.dart` | Material 3 入口 + 命名路由 (6 个) | ✅ |
| `config.dart` | API URL + BLE UUID + ServerConfig | ✅ |
| `models/device.dart` | DeviceModel 手写序列化 | ✅ |
| `models/weight_record.dart` | WeightRecord + WeightStats | ✅ |
| `models/command.dart` | CmdRequest + CmdResponse | ⚠️ 已定义但未使用 |
| `services/api_service.dart` | dio HTTP REST 客户端 | ✅ |
| `services/ble_service.dart` | BLE 扫描/连接/配网/命令/getStatus | ✅ |
| `services/ws_service.dart` | WebSocket 客户端 + 重连 | ✅ |
| `services/saved_measurement_store.dart` | 本地记录持久化 (SharedPreferences) | ✅ |
| `providers/app_providers.dart` | 6 个 Riverpod providers | ✅ |
| `screens/device_list_screen.dart` | 设备列表 (远程 + BLE 双 Tab + Server 入口) | ✅ |
| `screens/provision_screen.dart` | 多步配网向导 (4 步, 含凭据记忆 + 设备配置预读) | ✅ |
| `screens/device_detail_screen.dart` | 实时称重 + 图表 + Record + History | ✅ |
| `screens/calibration_screen.dart` | 校准向导 | ⚠️ 去皮按钮逻辑有误 |
| `screens/device_settings_screen.dart` | 设备设置编辑 (含 HTTP/MQTT 配置) | ✅ |
| `screens/settings_screen.dart` | APP 全局设置 (Server URL) | ⚠️ 清除数据未实现 |
| `screens/history_screen.dart` | 本地记录历史列表 | ✅ |
| `widgets/weight_display.dart` | 大字号重量显示 | ✅ |
| `widgets/weight_chart.dart` | fl_chart 折线图 | ✅ |
| `widgets/device_card.dart` | 设备列表卡片 | ✅ |
| `widgets/connection_indicator.dart` | BLE 连接指示器 | ✅ |
| `widgets/ble_scan_list.dart` | BLE 扫描结果列表 | ✅ |

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

### 已实现命令

| 命令 | 参数 | BLE | MQTT cmd | 说明 |
|------|------|-----|----------|------|
| `tare` | `{}` | ✅ | ❌ | 清零 (20 采样) |
| `calibrate` | `{"expected_weight": 500.0}` | ✅ | ❌ | 用已知重量校准 |
| `set_mode` | `{"mode": 0/1/2}` | ✅ | ❌ | 切换上传模式 |
| `set_config` | `{...}` | ✅ | ❌ | 修改配置 (mode/server_url/mqtt/unit/interval) |
| `set_wifi` | `{"ssid":"...","password":"..."}` | ✅ | ❌ | 更新 WiFi 并重连 |
| `set_mqtt` | `{"host":"...","port":1883,...}` | ✅ | ❌ | 更新 MQTT 并重连 |
| `get_status` | `{}` | ✅ | ❌ | 返回完整设备状态 |
| `reboot` | `{}` | ✅ | ❌ | 重启设备 |
| `factory_reset` | `{}` | ✅ | ❌ | 清除所有 NVS 并重启 |

## 开发命令

PlatformIO CLI 位于 `~/.platformio/penv/bin/pio`，配置 alias 或在固件目录下直接调用：

```bash
# 固件
cd firmware && ~/.platformio/penv/bin/pio run              # 编译
cd firmware && ~/.platformio/penv/bin/pio run -t upload    # 编译并烧录
cd firmware && ~/.platformio/penv/bin/pio device monitor   # 串口监视 (115200)

# 服务端
docker compose up -d                                       # 启动 Mosquitto + FastAPI
open http://localhost:8000/docs                            # OpenAPI 文档

# APP
cd app && flutter pub get && flutter run                   # 运行 (默认平台)
cd app && flutter run -d macos                             # macOS 桌面
```

## 资源约束

ESP32-C3:
- SRAM: ~400KB（可用 ~320KB after bootloader）
- Flash: 4MB（可用 ~3MB after partition table）
- 目标空闲堆: >50KB
- JSON 文档大小限制: 512 字节
- BLE MTU: 默认 256 字节

## 后续开发路线

### Phase 5: Bug 修复 + 完善 (高优先级)

1. **固件 MQTT 修复**
   - 修复 MQTT 自动连接触发 (`DISCONNECTED → CONNECTING`)
   - 添加 LWT 到 `espscale/{id}/status`
   - 订阅 `espscale/{id}/cmd` 并执行远程命令
   - 发布 status (online/offline) 到 status topic

2. **固件 BLE 完善**
   - 为 ScaleSettings 特征值添加 read/write 回调

3. **APP Bug 修复**
   - 修复路由名 `/settings` → `/device-settings`
   - 修复 `DropdownButtonFormField` 参数名
   - 修复校准页面去皮按钮逻辑
   - 统一 Mode 表示 (建议用整数 0/1/2 全局统一)

4. **APP 功能完善**
   - 实现 "清除数据" 功能
   - 移除硬编码 API Key，使用服务器返回的 key

### Phase 6: 安全 + 用户系统 (中优先级)

5. **服务端用户认证**
   - 添加 `POST /api/v1/auth/register` 和 `POST /api/v1/auth/login`
   - 设备管理端点添加 JWT 认证保护
   - WebSocket 连接添加 token 验证

6. **Alembic 迁移**
   - 初始化 Alembic，创建首次迁移
   - 替换 `create_all` 为迁移管理

### Phase 7: 体验优化 (低优先级)

7. **图表增强** — 时间轴标签、时间范围选择器 (1h/6h/24h/7d)
8. **固件 OTA** — 通过 HTTP 远程更新固件
9. **多语言** — APP 中英文国际化
10. **测试** — 单元测试 + 集成测试覆盖
