# 服务端实施计划 (server/)

> 状态：Phase 3 待开始

## 目标

构建数据收集与服务 API，接收多设备称重数据并存储，为 APP 端提供查询与实时推送能力。

## 技术栈

| 项目 | 选型 | 原因 |
|------|------|------|
| 语言/运行时 | Python 3.11+ | 快速迭代，生态丰富 |
| Web 框架 | FastAPI | 原生 async, 自动 OpenAPI docs, WebSocket 内置 |
| ORM | SQLAlchemy 2.0 (async) | Python ORM 标准, async session |
| 数据库 | SQLite (aiosqlite) | 零配置，单文件，适合本项目规模 |
| 数据校验 | Pydantic v2 | 类型安全，FastAPI 内置 |
| MQTT 客户端 | paho-mqtt | Python MQTT 标准库 |
| JWT | python-jose | 用户认证 |
| 部署 | Docker / Docker Compose | 与 Mosquitto 共同编排 |
| 迁移 | Alembic | 数据库 schema 版本管理 |

## 项目结构

```
server/
├── main.py                # FastAPI app, lifespan events (启动/停止 MQTT bridge)
├── config.py              # 环境变量配置 (DATABASE_URL, MQTT_HOST, SECRET_KEY...)
├── database.py            # SQLAlchemy async engine + async session factory
├── models.py              # ORM 模型: Device, WeightRecord, Event
├── schemas.py             # Pydantic schemas: 请求/响应/WebSocket
├── deps.py                # FastAPI 依赖注入 (get_db, get_current_user)
├── auth.py                # JWT 签发/验证, API Key 验证
├── routes/
│   ├── __init__.py
│   ├── devices.py         # 设备注册 + CRUD
│   ├── data.py            # 数据摄入 + 查询 + 统计 + 导出
│   └── ws.py              # WebSocket 端点 + 设备订阅管理
├── mqtt_bridge.py         # MQTT 订阅者 → 写DB → WebSocket 广播
├── requirements.txt
├── Dockerfile
└── alembic/
    ├── alembic.ini
    ├── env.py
    └── versions/
        └── 001_initial.py
```

## 数据库 Schema

### devices 表

```sql
CREATE TABLE devices (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id         TEXT UNIQUE NOT NULL,       -- "esp32c3-a1b2c3"
    name              TEXT DEFAULT '',             -- 用户可编辑的设备名称
    api_key           TEXT NOT NULL,               -- SHA256 hash of device api_key
    cal_factor        REAL DEFAULT 397.6,
    unit              TEXT DEFAULT 'g',
    upload_interval_ms INTEGER DEFAULT 5000,
    mode              TEXT DEFAULT 'remote',       -- 'remote' or 'local'
    last_weight       REAL,                        -- 最新重量值
    last_seen         TIMESTAMP,                   -- 最后数据时间
    is_online         BOOLEAN DEFAULT 0,           -- 在线状态 (由LWT更新)
    mqtt_broker       TEXT DEFAULT '',
    wifi_ssid         TEXT DEFAULT '',
    firmware_ver      TEXT DEFAULT '',
    created_at        TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at        TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_devices_device_id ON devices(device_id);
```

### weight_records 表

```sql
CREATE TABLE weight_records (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id       TEXT NOT NULL,              -- FK → devices(device_id)
    weight          REAL NOT NULL,
    unit            TEXT DEFAULT 'g',
    raw_value       INTEGER,                    -- HX711 raw reading
    stable          BOOLEAN DEFAULT 1,
    sequence_number INTEGER,                    -- device-side seq
    timestamp       TIMESTAMP NOT NULL,          -- device-reported time
    received_at     TIMESTAMP DEFAULT CURRENT_TIMESTAMP, -- server receipt time
    FOREIGN KEY (device_id) REFERENCES devices(device_id)
);

CREATE INDEX idx_weight_device_time ON weight_records(device_id, timestamp DESC);
CREATE INDEX idx_weight_received ON weight_records(received_at);
```

### events 表

```sql
CREATE TABLE events (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id       TEXT NOT NULL,
    event_type      TEXT NOT NULL,              -- 'online', 'offline', 'cmd_ack', 'error'
    payload         TEXT,                        -- 完整 JSON
    created_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

## API 端点设计

所有端点前缀 `/api/v1`。设备端用 `X-API-Key` 头认证，APP 端用 JWT Bearer Token。

### 设备管理 (JWT 认证)

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/api/v1/devices/register` | 注册新设备 (API Key 认证) |
| `GET` | `/api/v1/devices` | 列出用户所有设备 |
| `GET` | `/api/v1/devices/{device_id}` | 设备详情 |
| `PUT` | `/api/v1/devices/{device_id}` | 更新设备配置 (name/interval 等) |
| `DELETE` | `/api/v1/devices/{device_id}` | 删除设备及其所有数据 |

### 数据摄入 (API Key 认证)

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/api/v1/data` | 单条重量数据上传 (HTTP 回退) |
| `POST` | `/api/v1/data/batch` | 批量上传 (离线积压数据) |

### 数据查询 (JWT 认证)

| Method | Path | Query Params | Description |
|--------|------|-------------|-------------|
| `GET` | `/api/v1/devices/{id}/records` | `from`, `to`, `limit`, `offset` | 分页历史数据 |
| `GET` | `/api/v1/devices/{id}/records/latest` | — | 最新一次称重 |
| `GET` | `/api/v1/devices/{id}/records/stats` | `from`, `to` | min/max/avg/count |
| `GET` | `/api/v1/devices/{id}/records/export` | `from`, `to` | CSV 导出 |

### WebSocket (JWT 认证)

| Path | Description |
|------|-------------|
| `WS /ws` | 用户所有设备实时数据 (query: `?token=<jwt>`) |
| `WS /ws/{device_id}` | 单设备实时数据 |

**WebSocket 消息格式:**

```json
{
  "type": "weight_update",
  "device_id": "esp32c3-a1b2c3",
  "data": {
    "weight": 123.45,
    "unit": "g",
    "timestamp": 1749876543,
    "seq": 1042
  }
}
```

```json
{
  "type": "device_status",
  "device_id": "esp32c3-a1b2c3",
  "data": {
    "status": "online",
    "wifi_rssi": -45,
    "uptime_s": 3600
  }
}
```

## MQTT Bridge

服务器内部运行 paho-mqtt 客户端作为后台任务（FastAPI lifespan 管理生命周期）：

```
Mosquitto (port 1883)
    │
    │  topic 订阅: espscale/+/weight, espscale/+/status
    │
    ▼
mqtt_bridge.py:
    on_connect:
        subscribe("espscale/+/weight", qos=1)
        subscribe("espscale/+/status", qos=1)

    on_message(topic, payload):
        device_id = parse from topic (espscale/{device_id}/weight)
        if topic ends with /weight:
            json = parse payload
            insert into weight_records
            update devices.last_weight, devices.last_seen, devices.is_online=True
            broadcast via WebSocket to subscribed app clients
        if topic ends with /status:
            json = parse payload
            if json.status == "online":
                update devices.is_online=True, devices.last_seen
            elif json.status == "offline" (LWT):
                update devices.is_online=False
            broadcast via WebSocket
```

## 认证设计

### 设备端 (API Key)

- 设备首次配网时，固件生成 32 字符的随机 hex API Key 并存储在 NVS
- 服务端注册设备时存储 API Key 的 SHA256 hash
- 设备每次 HTTP POST 数据时，在 `X-API-Key` 头携带 API Key
- 服务端验证: `hash(header_api_key) == stored_hash`

### APP 端 (JWT)

- 用户在 APP 注册/登录，服务端签发 JWT (expire: 7 days)
- APP 在 HTTP 请求头 `Authorization: Bearer <jwt>` 携带
- WebSocket: `ws://host/ws?token=<jwt>`

## Docker Compose 部署

```yaml
# docker-compose.yml (项目根目录)
services:
  mosquitto:
    image: eclipse-mosquitto:2
    ports:
      - "1883:1883"
      - "9001:9001"    # MQTT over WebSocket (可选)
    volumes:
      - ./mosquitto/config:/mosquitto/config
      - ./mosquitto/data:/mosquitto/data
    restart: unless-stopped

  server:
    build: ./server
    ports:
      - "8000:8000"
    volumes:
      - ./server:/app
      - ./data:/app/data       # SQLite DB 持久化
    environment:
      - DATABASE_URL=sqlite+aiosqlite:///app/data/espscale.db
      - MQTT_BROKER_HOST=mosquitto
      - MQTT_BROKER_PORT=1883
      - SECRET_KEY=${SECRET_KEY}
      - ACCESS_TOKEN_EXPIRE_MINUTES=10080  # 7 days
    depends_on:
      - mosquitto
    restart: unless-stopped
```

## 实施任务

### Phase 3: 服务端实现 (5-7 天)

1. 项目初始化
   - `server/` 目录，`pyproject.toml` / `requirements.txt`
   - FastAPI app skeleton + lifespan
   - 环境变量配置 (`config.py`)

2. 数据库
   - SQLAlchemy async engine + session (`database.py`)
   - ORM 模型 (`models.py`)
   - Alembic 初始化迁移

3. 认证模块
   - `auth.py`: JWT 签发/验证 + API Key hash 验证
   - `deps.py`: `get_db`, `get_current_user` 依赖注入

4. REST API
   - `routes/devices.py`: 设备 CRUD
   - `routes/data.py`: 数据摄入 + 查询 + 统计 + 导出
   - Pydantic schemas (`schemas.py`)

5. WebSocket
   - `routes/ws.py`: 连接管理 + 设备订阅 + 消息广播
   - 用户 → WebSocket 连接 → 按 device_id 订阅

6. MQTT Bridge
   - `mqtt_bridge.py`: paho-mqtt 订阅 + 写 DB + WS 广播
   - FastAPI lifespan 管理启动/停止

7. Docker 部署
   - `Dockerfile` (FastAPI)
   - `docker-compose.yml` (Mosquitto + FastAPI)
   - `mosquitto/config/mosquitto.conf`

8. 测试
   - 启动 Docker Compose
   - 设备注册 → POST 数据 → API 查询 → WebSocket 推送

## 验证方案

```bash
# 启动服务
docker compose up -d

# 查看 API 文档
open http://localhost:8000/docs

# 模拟设备注册 + 数据上传
curl -X POST http://localhost:8000/api/v1/devices/register \
  -H "X-API-Key: testkey123" \
  -H "Content-Type: application/json" \
  -d '{"device_id":"esp32c3-test1234","firmware_ver":"1.0.0"}'

curl -X POST http://localhost:8000/api/v1/data \
  -H "X-Device-ID: esp32c3-test1234" \
  -H "X-API-Key: testkey123" \
  -H "Content-Type: application/json" \
  -d '{"weight":123.45,"unit":"g","raw":123456,"stable":true,"timestamp":1749876543,"seq":1}'

# 查询数据
curl http://localhost:8000/api/v1/devices/esp32c3-test1234/records/latest

# WebSocket 测试
websocat ws://localhost:8000/ws/esp32c3-test1234?token=<jwt>

# MQTT 测试 (模拟设备发布)
mosquitto_pub -h localhost -t "espscale/esp32c3-test1234/weight" \
  -m '{"device_id":"esp32c3-test1234","weight":456.78,"unit":"g","stable":true,"timestamp":1749876543,"seq":2}' -q 1
```
