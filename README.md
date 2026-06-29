# ESPScale

> **v0.2.0** — 基于 ESP32-C3 + HX711 的物联网称重系统，支持 BLE 配网、本地称重与远程数据上传。

## 架构概览

```
┌──────────────────┐     MQTT (JSON)      ┌──────────────────┐
│  ESP32-C3 设备    │ ──────────────────> │  Mosquitto Broker │
│  (HX711 称重)     │  espscale/{id}/     │  (Docker)         │
│                   │    weight | status  └────────┬─────────┘
│  模式A: MQTT远程   │                              │ subscribe
│  模式B: BLE本地秤  │                              ▼
│  模式C: HTTP直传   │                     ┌──────────────────┐
└────────┬─────────┘                     │  FastAPI 服务器    │
         │ BLE GATT (JSON)               │  + SQLite         │
         │ 配网/配置/校准/命令            │  + WebSocket      │
         ▼                               │  + REST API       │
┌──────────────────┐                     │  + MQTT Bridge    │
│  APP 端           │◄── HTTP/WebSocket ──┤                   │
│  (Flutter 6平台)  │                     └──────────────────┘
└──────────────────┘
```

### 三种运行模式

| 模式 | 常量 | 数据流 | 适用场景 |
|------|------|--------|----------|
| **MQTT 远程** | `MODE_MQTT=1` | 设备 → MQTT → 服务器 → WS/HTTP → APP | 生产部署，多设备管理 |
| **HTTP 直传** | `MODE_HTTP_DIRECT=0` | 设备 → HTTP POST → 服务器 → WS/HTTP → APP | 无 MQTT Broker 的轻量部署 |
| **BLE 本地** | `MODE_BLE_ONLY=2` | 设备 ↔ BLE GATT ↔ APP | 离线使用，无需网络 |

- **远程模式 (MQTT/HTTP)**：设备定时上报称重数据到服务器，APP 通过 WebSocket 实时接收
- **本地模式 (BLE)**：设备通过 BLE Notify 实时推流，APP 直连控制（清零/校准/设置）

## 项目结构

```
ESPScale/
├── firmware/              # ESP32-C3 PlatformIO 项目（设备端）
│   ├── platformio.ini     # 构建配置 + 依赖
│   ├── src/
│   │   ├── main.cpp       # 入口, setup()/loop() 非阻塞调度
│   │   ├── config.h       # 引脚/UUID/常量/模式定义
│   │   ├── utils.h        # 日志宏 + deviceId 生成
│   │   ├── storage.h/.cpp        # NVS 持久化封装 (Preferences)
│   │   ├── state_machine.h/.cpp  # 8 状态 FSM + LED 指示
│   │   ├── ble_server.h/.cpp     # NimBLE GATT 服务 (7 特征值)
│   │   ├── ble_protocol.h/.cpp   # BLE 回调 + 命令分发 (9 命令)
│   │   ├── wifi_manager.h/.cpp   # WiFi STA 连接 + 退避重试
│   │   ├── mqtt_client.h/.cpp    # MQTT 非阻塞发布
│   │   ├── http_client.h/.cpp    # HTTP POST 状态机
│   │   ├── scale_sensor.h/.cpp   # HX711 读取/去皮/校准
│   │   └── protocol.h/.cpp       # 统一命令协议解析
│   └── scripts/ble.py     # BLE 开发调试脚本
│
├── server/                # Python FastAPI 服务端
│   ├── main.py            # FastAPI app + lifespan
│   ├── config.py          # 环境变量配置
│   ├── database.py        # SQLAlchemy async engine + session
│   ├── models.py          # ORM: Device, WeightRecord, Event
│   ├── schemas.py         # Pydantic v2 schemas
│   ├── auth.py            # JWT + API Key 认证工具
│   ├── deps.py            # FastAPI 依赖注入
│   ├── mqtt_bridge.py     # MQTT → DB → WebSocket 桥接
│   ├── routes/
│   │   ├── devices.py     # 设备注册 + CRUD (5 端点)
│   │   ├── data.py        # 数据摄入 + 查询 + 统计 + CSV 导出 (7 端点)
│   │   └── ws.py          # WebSocket 实时推送 (2 端点)
│   ├── requirements.txt
│   └── Dockerfile
│
├── app/                   # Flutter APP 端
│   ├── lib/
│   │   ├── main.dart          # Material 3 入口 + 路由
│   │   ├── config.dart        # API URL + BLE UUID 常量
│   │   ├── models/            # Device, WeightRecord, Command 模型
│   │   ├── services/          # BLE / API / WebSocket 服务
│   │   ├── providers/         # Riverpod 状态管理
│   │   ├── screens/           # 6 个页面 (列表/配网/详情/校准/设置)
│   │   └── widgets/           # 5 个可复用组件
│   └── pubspec.yaml
│
├── docker-compose.yml     # Mosquitto + FastAPI 编排
├── mosquitto/             # MQTT Broker 配置
├── docs/                  # 功能文档 + 环境搭建 + 构建指南
├── plans/                 # 各模块详细实施计划
├── scripts/               # 开发工具脚本
└── CLAUDE.md              # 技术选型与开发规范
```

## 快速开始

### 前置要求

- [PlatformIO IDE](https://platformio.org/) 或 PlatformIO Core CLI
- [Docker](https://www.docker.com/) + Docker Compose (服务端)
- [Flutter](https://flutter.dev/) 3.x+ (APP 端)
- Python 3.11+ (服务端开发)

### 1. 启动服务端

```bash
docker compose up -d          # 启动 Mosquitto (1883) + FastAPI (8000)
# API 文档: http://localhost:8000/docs
```

### 2. 烧录固件

```bash
cd firmware
~/.platformio/penv/bin/pio run -t upload   # 编译并烧录到 ESP32-C3
~/.platformio/penv/bin/pio device monitor   # 串口监视 (115200 baud)
```

首次启动进入 **PROVISIONING** 状态（LED 慢闪），等待 BLE 配网。

### 3. 运行 APP

```bash
cd app
flutter pub get
flutter run                   # 桌面端: flutter run -d macos
```

APP 扫描到设备 → 连接 → 输入 WiFi 凭据 → 配网完成 → 自动跳转详情页。

## 技术栈

| 层级 | 技术 | 说明 |
|------|------|------|
| 设备端 | C++ (Arduino), PlatformIO, NimBLE-Arduino, HX711, PubSubClient, ArduinoJson | ESP32-C3 (RISC-V, 400KB SRAM) |
| 服务端 | Python FastAPI, SQLAlchemy 2.0 (async), aiosqlite, paho-mqtt, python-jose | Docker 部署 |
| APP 端 | Flutter 3.x, flutter_blue_plus, fl_chart, Riverpod, dio, web_socket_channel | 6 平台 |
| 消息 | MQTT (Mosquitto), WebSocket, HTTP REST | JSON 编码 |
| 部署 | Docker Compose | Mosquitto + FastAPI 一键启动 |

## 已实现功能

### 固件端 (v0.2.0)

- [x] **BLE 配网** — APP 通过 BLE 写入 WiFi 凭据，持久化到 NVS，断电不丢失
- [x] **BLE GATT 服务** — 7 个特征值 (DeviceInfo, WifiCreds, NetworkStatus, ScaleSettings, WeightStream, Command, Event)
- [x] **9 条 BLE 命令** — tare, calibrate, set_mode, set_config, set_wifi, set_mqtt, get_status, reboot, factory_reset
- [x] **8 状态 FSM** — PROVISIONING / CONNECTING_WIFI / CONNECTING_MQTT / RUNNING / ERROR_WIFI / ERROR_MQTT / ERROR_HX711 / FACTORY_RESET
- [x] **GPIO8 LED 指示** — 不同闪烁模式对应设备状态
- [x] **NVS 持久化** — WiFi/MQTT 配置、校准系数、设备 ID、模式等
- [x] **HTTP POST 上传** — 非阻塞状态机，支持 X-Device-ID + X-API-Key 认证
- [x] **MQTT 发布** — PubSubClient 非阻塞封装，称重数据发布到 `espscale/{id}/weight`
- [x] **HX711 传感器** — 非阻塞读取、去皮、校准（计算新系数并保存）
- [x] **WiFi 管理** — STA 模式，自动重连，退避重试（最多 3 次）

### 服务端 (v0.2.0)

- [x] **设备注册 + CRUD** — `POST /register`, `GET /`, `GET /{id}`, `PUT /{id}`, `DELETE /{id}`
- [x] **数据摄入** — `POST /data` (单条), `POST /data/batch` (批量)，API Key 认证
- [x] **历史查询** — 分页查询、时间过滤、最新记录、统计 (min/max/avg/count)
- [x] **CSV 导出** — `GET /records/export` 按时间范围下载
- [x] **WebSocket 实时推送** — 多设备订阅 (`/ws`) + 单设备订阅 (`/ws/{device_id}`)
- [x] **MQTT Bridge** — 订阅 `espscale/+/weight` 和 `espscale/+/status`，自动写 DB + WS 广播
- [x] **JWT + API Key 认证** — 设备端 API Key (SHA256 哈希存储)，用户端 JWT
- [x] **Docker Compose** — Mosquitto + FastAPI 一键部署

### APP 端 (v0.2.0)

- [x] **BLE 扫描 + 连接** — 按 Service UUID 过滤，RSSI 信号强度显示
- [x] **多步配网向导** — 读设备信息 + 预读设备配置 → 输入 WiFi/模式 → 等待连接 → 注册到服务器
- [x] **三种模式配网** — HTTP Direct / MQTT / BLE Only，根据模式显示不同配置字段
- [x] **WiFi 凭证记忆** — 配网成功后 SharedPreferences 持久化，下次自动回填
- [x] **设备配置预读** — 配网前从设备 `get_status` 读取当前 server_url/mqtt_host/port 预填
- [x] **设备列表** — 双 Tab (远程 API + 本地 BLE)，设备卡片显示在线状态/最后重量
- [x] **APP 全局设置** — 设备列表 AppBar 可配置 Remote server 地址 (含 dns 图标入口)
- [x] **实时称重** — 大字号显示 (64px)，稳定/测量指示，**BLE 优先**，WebSocket 仅作后备
- [x] **历史图表** — fl_chart 折线图，曲线面积填充，触摸提示
- [x] **命令控制** — 清零 (tare)、校准 (calibrate) — 通过 BLE Command 特征值
- [x] **Record 按钮** — 一键保存当前重量到本地历史，离线可用
- [x] **History 页面** — 本地保存的测量记录列表，支持删除，相对时间显示
- [x] **设备设置** — 编辑名称/单位/上传间隔/模式/HTTP server URL/MQTT broker+port (通过 API)
- [x] **WebSocket 重连** — 指数退避，最多 5 次
- [x] **API Key 动态生成** — APP 生成 32 字符 hex，通过 `set_config` 命令下发到固件 NVS

## API 端点概览

| 分类 | Method | Path | 认证 | 说明 |
|------|--------|------|------|------|
| 设备 | `POST` | `/api/v1/devices/register` | — | 注册新设备 |
| 设备 | `GET` | `/api/v1/devices` | — | 列出所有设备 |
| 设备 | `GET` | `/api/v1/devices/{id}` | — | 设备详情 |
| 设备 | `PUT` | `/api/v1/devices/{id}` | — | 更新设备 |
| 设备 | `DELETE` | `/api/v1/devices/{id}` | — | 删除设备 |
| 数据 | `POST` | `/api/v1/data` | API Key | 单条数据上传 |
| 数据 | `POST` | `/api/v1/data/batch` | API Key | 批量上传 |
| 查询 | `GET` | `/api/v1/devices/{id}/records` | — | 分页历史 |
| 查询 | `GET` | `/api/v1/devices/{id}/records/latest` | — | 最新记录 |
| 查询 | `GET` | `/api/v1/devices/{id}/records/stats` | — | 统计信息 |
| 查询 | `GET` | `/api/v1/devices/{id}/records/export` | — | CSV 导出 |
| WS | `WS` | `/ws` | — | 多设备实时流 |
| WS | `WS` | `/ws/{device_id}` | — | 单设备实时流 |

## 已知问题

### 固件端
| 问题 | 严重度 | 说明 |
|------|--------|------|
| MQTT 客户端不会自动连接 | 🔴 严重 | `mqtt_client` 状态机缺少 `DISCONNECTED → CONNECTING` 触发，MQTT 模式下无法自动建连 |
| ScaleSettings BLE 特征值无回调 | 🟡 中等 | 声明 READ+WRITE 但未设置 handler，读写操作被忽略 |
| MQTT 缺少 LWT | 🟡 中等 | 未在 connect 时设置 Last Will，status topic 未发布 |
| MQTT 缺少 cmd 订阅 | 🟡 中等 | 未订阅 `espscale/{id}/cmd` topic，无法远程命令 |
| `scaleCalibrate()` 中有 `delay(500)` | 🟢 低 | 阻塞调用但仅通过 BLE 命令触发，不在主循环中 |
| Network Status MQTT 状态始终 false | 🟢 低 | BLE 特征值中 `mqtt.connected` 硬编码为 false |

### APP 端
| 问题 | 严重度 | 说明 |
|------|--------|------|
| 校准页面 "去皮" 按钮 | 🟡 功能缺失 | Step 1 按钮调用 `_calibrate()` 而非发送 `tare` 命令 |
| "清除数据" 按钮空实现 | 🟡 未完成 | `settings_screen.dart` 中 onTap 为空 |
| mode 切换 UX 不一致 | 🟡 UX | 详情页 Mode 按钮已改为 Record（更好的 UX），但 mode 切换现在需进设置页 |
| 多个生产代码 `print()` | 🟢 清理 | BLE/Detail 屏有调试 print 未替换为正式 logger |
| 校准/历史时间轴缺时间标签 | 🟢 体验 | 折线图 X 轴无时间刻度 |

### 服务端
| 问题 | 严重度 | 说明 |
|------|--------|------|
| 大部分端点无认证保护 | 🟡 安全 | 设备列表/记录查询/WS 均为公开访问 |
| 无用户登录/注册端点 | 🟡 功能缺失 | JWT 工具已实现但无签发入口 |
| Alembic 未配置 | 🟢 待办 | 使用 `create_all` 代替迁移 |
| `deps.py` 中 `get_current_device` 未使用 | 🟢 死代码 | `data.py` 自行实现了 `_auth_device()` |
| `EventResponse` schema 未使用 | 🟢 死代码 | 已定义但无 `GET /events` 端点 |

## 相关文档

| 文档 | 说明 |
|------|------|
| [CLAUDE.md](CLAUDE.md) | 技术选型与开发规范 |
| [docs/firmware.md](docs/firmware.md) | 固件功能说明、BLE GATT、状态机、配网流程 |
| [docs/environment.md](docs/environment.md) | 开发环境搭建（PlatformIO / Python / Flutter / Docker） |
| [docs/build.md](docs/build.md) | 多端编译与发布（固件烧录 / Docker 部署 / Flutter 6 平台） |
| [plans/firmware.md](plans/firmware.md) | 设备端实施计划（Phase 1-2 ✅ 已完成） |
| [plans/server.md](plans/server.md) | 服务端实施计划（Phase 3 ✅ 已完成） |
| [plans/app.md](plans/app.md) | APP 端实施计划（Phase 4 ✅ 已完成） |
