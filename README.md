# ESPScale

基于 ESP32-C3 + HX711 的物联网称重系统，支持 BLE 本地称重与 WiFi/MQTT 远程数据上传。

## 架构概览

```
┌──────────────────┐     MQTT (JSON)      ┌──────────────────┐
│  ESP32-C3 设备    │ ──────────────────> │  Mosquitto Broker │
│  (HX711 称重)     │  espscale/{id}/     │  (Docker)         │
│                   │    weight | status  └────────┬─────────┘
│  模式A: 远程上传   │                              │ subscribe
│  模式B: 本地BLE秤 │                              ▼
└────────┬─────────┘                     ┌──────────────────┐
         │ BLE GATT (JSON)               │  FastAPI 服务器    │
         │ 本地称重/配置/校准             │  + SQLite         │
         ▼                               │  + WebSocket      │
┌──────────────────┐                     │  + REST API       │
│  APP 端           │◄── HTTP/WebSocket ──┤                   │
│  (Flutter)        │                     └──────────────────┘
└──────────────────┘
```

- **远程模式**：设备 → MQTT → 服务器 → HTTP/WebSocket → APP
- **本地模式**：设备 ↔ BLE GATT ↔ APP（直连，无需服务器）

## 项目结构

```
ESPScale/
├── firmware/              # ESP32-C3 PlatformIO 项目（设备端）
│   ├── platformio.ini
│   ├── src/
│   │   ├── main.cpp       # 入口, setup()/loop() 调度
│   │   ├── config.h       # 引脚/UUID/常量定义
│   │   ├── state_machine  # 状态机 + LED 指示
│   │   ├── storage        # NVS 持久化封装
│   │   ├── ble_server     # BLE GATT 服务初始化
│   │   ├── ble_protocol   # BLE 回调 + JSON 解析
│   │   ├── wifi_manager   # WiFi STA 连接/重连
│   │   ├── mqtt_client    # MQTT 非阻塞客户端
│   │   ├── http_client    # HTTP POST 回退
│   │   ├── scale_sensor   # HX711 称重传感器
│   │   ├── protocol       # 统一命令协议
│   │   └── utils          # 工具函数
│   └── include/
│
├── server/                # Python FastAPI 服务端
│   ├── main.py            # 入口, lifespan events
│   ├── config.py          # 环境配置
│   ├── database.py        # SQLAlchemy async engine
│   ├── models.py          # ORM 模型
│   ├── schemas.py         # Pydantic schemas
│   ├── routes/            # REST API 路由
│   ├── mqtt_bridge.py     # MQTT → DB 桥接
│   ├── auth.py            # JWT + API Key 认证
│   ├── requirements.txt
│   └── Dockerfile
│
├── app/                   # Flutter APP 端
│   └── lib/
│       ├── models/        # 数据模型
│       ├── services/      # BLE/API/WS/MQTT 通信
│       ├── providers/     # 状态管理 (Riverpod)
│       ├── screens/       # 页面
│       └── widgets/       # UI 组件
│
├── docker-compose.yml     # Mosquitto + FastAPI 部署
├── scripts/               # 开发工具脚本
├── docs/                  # 协议文档
├── plans/                 # 各模块详细计划
└── README.md
```

## 快速开始

### 前置要求

- [PlatformIO IDE](https://platformio.org/) 或 PlatformIO Core (CLI)
- [Docker](https://www.docker.com/) + Docker Compose (服务端)
- [Flutter](https://flutter.dev/) 3.x+ (APP 端)
- Python 3.11+ (服务端开发/脚本)

### 设备端（固件）

```bash
cd firmware
pio run -t upload       # 编译并烧录
pio device monitor      # 串口监视 (115200 baud)
```

### 服务端

```bash
docker compose up -d    # 启动 Mosquitto + FastAPI
# API 文档: http://localhost:8000/docs
```

### APP 端

```bash
cd app
flutter pub get
flutter run              # 桌面端: flutter run -d macos
```

## 技术栈

| 层级 | 技术 | 说明 |
|------|------|------|
| 设备端 | C++ (Arduino), PlatformIO, NimBLE-Arduino, HX711, PubSubClient, ArduinoJson | ESP32-C3 |
| 服务端 | Python FastAPI, SQLAlchemy (async), aiosqlite, paho-mqtt | Docker 部署 |
| APP 端 | Flutter, flutter_blue_plus, fl_chart, Riverpod | 跨平台 6 端 |
| 消息 | MQTT (Mosquitto), WebSocket, HTTP REST | JSON 编码 |

## 主要功能

- **BLE 配网**：类似小米 IoT 设备，无需硬编码 WiFi 凭据，通过 BLE 发送并持久化到 NVS
- **双模式运行**：远程上传（MQTT 定时上报）与本地称重（BLE 实时推流）自由切换
- **统一命令协议**：BLE 和 MQTT 共用同一 JSON 命令格式，支持清零、校准、配置修改
- **持久化存储**：WiFi/MQTT 配置、校准系数、设备 ID 等保存在 ESP32 NVS，断电不丢失
- **HTTP 回退**：MQTT 不可用时自动切换到 HTTP POST 上传
- **LED 状态指示**：GPIO8 用不同闪烁模式表示设备当前状态
- **WebSocket 实时推送**：服务端通过 WebSocket 向 APP 实时推送称重数据
- **历史数据查询**：支持按时间范围查询、统计（min/max/avg）、CSV 导出

## 通信协议

详见 [docs/protocol.md](docs/protocol.md) 获取完整 BLE GATT 规范、MQTT Topic 设计和命令协议。

## 相关文档

| 文档 | 说明 |
|------|------|
| [docs/firmware.md](docs/firmware.md) | 固件功能说明、BLE GATT、状态机、配网流程 |
| [docs/environment.md](docs/environment.md) | 开发环境搭建（PlatformIO/Python/Flutter/Docker） |
| [docs/build.md](docs/build.md) | 多端编译与发布（固件烧录/Docker 部署/Flutter 6 平台） |
| [plans/firmware.md](plans/firmware.md) | 设备端详细实施计划 |
| [plans/server.md](plans/server.md) | 服务端详细实施计划 |
| [plans/app.md](plans/app.md) | APP 端详细实施计划 |
| [CLAUDE.md](CLAUDE.md) | 技术选型与开发规范 |
