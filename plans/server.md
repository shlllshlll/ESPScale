# 服务端实施计划 (server/)

> **状态：Phase 3 ✅ 已实现** | 版本 v0.2.0

## 目标

构建数据收集与服务 API，接收多设备称重数据并存储，为 APP 端提供查询与实时推送能力。

## 技术栈

| 项目 | 选型 | 状态 |
|------|------|------|
| 语言/运行时 | Python 3.11+ | ✅ |
| Web 框架 | FastAPI | ✅ v0.2.0 |
| ORM | SQLAlchemy 2.0 (async) | ✅ mapped_column 风格 |
| 数据库 | SQLite (aiosqlite) | ✅ |
| 数据校验 | Pydantic v2 | ✅ |
| MQTT 客户端 | paho-mqtt v2 (CallbackAPIVersion.VERSION2) | ✅ |
| JWT | python-jose (HS256) | ✅ (工具就绪，无签发端点) |
| 部署 | Docker / Docker Compose | ✅ |
| 迁移 | Alembic | ❌ 未配置 (用 create_all) |

## 项目结构 (实际)

```
server/
├── main.py                ✅ FastAPI app (v0.2.0) + lifespan + CORS
├── config.py              ✅ 环境变量配置
├── database.py            ✅ SQLAlchemy async engine + session + init_db
├── models.py              ✅ Device, WeightRecord, Event (SQLAlchemy 2.0)
├── schemas.py             ✅ Pydantic v2 (8 schemas)
├── deps.py                ✅ get_current_user + get_current_device (⚠️ 部分未使用)
├── auth.py                ✅ JWT 签发/验证 + API Key hash/verify/generate
├── routes/
│   ├── __init__.py
│   ├── devices.py         ✅ 设备注册 + CRUD (5 端点)
│   ├── data.py            ✅ 数据摄入 + 查询 + 统计 + CSV (7 端点)
│   └── ws.py              ✅ WebSocket (2 端点, ConnectionManager)
├── mqtt_bridge.py         ✅ MQTT → DB → WS 桥接 (paho-mqtt v2)
├── requirements.txt       ✅ 8 依赖
├── Dockerfile             ✅ Python 3.11-slim + uvicorn
└── espscale.db            # SQLite 数据库文件 (运行时生成)
```

## 数据库 Schema (已实现)

### devices 表

```sql
CREATE TABLE devices (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id         TEXT UNIQUE NOT NULL,       -- "esp32c3-a1b2c3"
    name              TEXT DEFAULT '',
    api_key_hash      TEXT NOT NULL,               -- SHA256 hash
    cal_factor        REAL DEFAULT 397.6,
    unit              TEXT DEFAULT 'g',
    upload_interval_ms INTEGER DEFAULT 5000,
    mode              TEXT DEFAULT 'http_direct',  -- 'http_direct'/'mqtt'/'ble_only'
    last_weight       REAL,
    last_seen         TIMESTAMP,
    is_online         BOOLEAN DEFAULT 0,
    server_url        TEXT DEFAULT '',
    mqtt_broker       TEXT DEFAULT '',
    wifi_ssid         TEXT DEFAULT '',
    firmware_ver      TEXT DEFAULT '',
    created_at        TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at        TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
-- Index: idx_devices_device_id ON device_id
```

### weight_records 表

```sql
CREATE TABLE weight_records (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id       INTEGER NOT NULL,              -- FK → devices.id
    weight          REAL NOT NULL,
    unit            TEXT DEFAULT 'g',
    raw_value       INTEGER,
    stable          BOOLEAN DEFAULT 1,
    sequence_number INTEGER,
    timestamp       TIMESTAMP NOT NULL,
    received_at     TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(id)
);
-- Index: idx_weight_device_time ON (device_id, timestamp DESC)
-- Index: idx_weight_received ON (received_at)
```

### events 表

```sql
CREATE TABLE events (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id       INTEGER NOT NULL,
    event_type      TEXT NOT NULL,                  -- 'online','offline','cmd_ack','error'
    payload         TEXT,
    created_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
-- Index: idx_events_device_id ON device_id
```

## API 端点 (已实现)

所有端点前缀 `/api/v1`。

### 设备管理 — `routes/devices.py`

| Method | Path | 认证 | 状态 | 说明 |
|--------|------|------|------|------|
| `POST` | `/api/v1/devices/register` | — | ✅ | 注册新设备 (生成 API Key, 存储 hash) |
| `GET` | `/api/v1/devices` | — | ✅ | 列出所有设备 (按 updated_at DESC) |
| `GET` | `/api/v1/devices/{device_id}` | — | ✅ | 设备详情 |
| `PUT` | `/api/v1/devices/{device_id}` | — | ✅ | 更新设备配置 (partial) |
| `DELETE` | `/api/v1/devices/{device_id}` | — | ✅ | 删除设备及关联数据 |

### 数据摄入 — `routes/data.py`

| Method | Path | 认证 | 状态 | 说明 |
|--------|------|------|------|------|
| `POST` | `/api/v1/data` | X-Device-ID + X-API-Key | ✅ | 单条重量上传 + WS 广播 |
| `POST` | `/api/v1/data/batch` | X-Device-ID + X-API-Key | ✅ | 批量上传 + WS 广播 |

### 数据查询 — `routes/data.py`

| Method | Path | Query Params | 状态 | 说明 |
|--------|------|-------------|------|------|
| `GET` | `/api/v1/devices/{id}/records` | `from`, `to`, `limit`, `offset` | ✅ | 分页历史 |
| `GET` | `/api/v1/devices/{id}/records/latest` | — | ✅ | 最新记录 |
| `GET` | `/api/v1/devices/{id}/records/stats` | `from`, `to` | ✅ | min/max/avg/count |
| `GET` | `/api/v1/devices/{id}/records/export` | `from`, `to` | ✅ | CSV 导出 |

### WebSocket — `routes/ws.py`

| Path | 状态 | 说明 |
|------|------|------|
| `WS /ws` | ✅ | 多设备订阅 (JSON subscribe/unsubscribe 消息) |
| `WS /ws/{device_id}` | ✅ | 单设备自动订阅 |

**WebSocket 消息格式:**

```json
// 称重更新
{"type": "weight_update", "device_id": "esp32c3-a1b2c3", "data": {"weight": 123.45, "unit": "g", ...}}

// 设备状态
{"type": "device_status", "device_id": "esp32c3-a1b2c3", "data": {"status": "online", ...}}
```

## MQTT Bridge (已实现)

`mqtt_bridge.py` 在 FastAPI lifespan 中启动/停止:

```
Mosquitto (port 1883)
    │
    │  订阅: espscale/+/weight (QoS 1), espscale/+/status (QoS 1)
    │
    ▼
on_message:
    /weight → 解析 JSON → 插入 weight_records → 更新 Device → WS 广播
    /status → 解析 status → 更新 is_online → 记录 Event → WS 广播
```

使用 paho-mqtt v2 API (`CallbackAPIVersion.VERSION2`, `connect_async`, `loop_start`)。

## 认证设计 (已实现)

### 设备端 (API Key) — ✅

- 注册时生成 32 hex 随机 API Key，存储 SHA256 hash
- HTTP 请求 `X-Device-ID` + `X-API-Key` 头认证
- `data.py` 中 `_auth_device()` 内联实现验证

### APP 端 (JWT) — ⚠️ 工具就绪，未使用

- `auth.py` 提供 `create_access_token()` / `verify_token()`
- `deps.py` 提供 `get_current_user` 依赖注入
- **缺少**: 用户注册/登录端点，目前无 JWT 签发入口
- 当前所有端点均无认证保护

## Docker Compose 部署 (已实现)

```yaml
services:
  mosquitto:
    image: eclipse-mosquitto:2
    ports: ["1883:1883"]
    volumes: ./mosquitto/config, ./mosquitto/data

  server:
    build: ./server
    ports: ["8000:8000"]
    volumes: ./server:/app, ./data:/app/data
    environment: DATABASE_URL, MQTT_BROKER_HOST=mosquitto, SECRET_KEY
    depends_on: mosquitto
```

## Pydantic Schemas (已实现)

| Schema | 类型 | 状态 |
|--------|------|------|
| `DeviceRegister` | Request | ✅ |
| `DeviceUpdate` | Request | ✅ (all Optional) |
| `DeviceResponse` | Response | ✅ |
| `WeightData` | Request | ✅ |
| `WeightBatch` | Request | ✅ |
| `WeightRecordResponse` | Response | ✅ |
| `WeightStats` | Response | ✅ (min/max/avg/count) |
| `EventResponse` | Response | ⚠️ 已定义但未使用 (无 events 查询端点) |

## 验证方案

```bash
# 启动服务
docker compose up -d
open http://localhost:8000/docs

# 模拟设备注册 + 数据上传
curl -X POST http://localhost:8000/api/v1/devices/register \
  -H "Content-Type: application/json" \
  -d '{"device_id":"esp32c3-test1234","firmware_ver":"1.0.0"}'
# → 返回 device_id + api_key (保存 api_key 用于后续请求)

curl -X POST http://localhost:8000/api/v1/data \
  -H "X-Device-ID: esp32c3-test1234" \
  -H "X-API-Key: <api_key>" \
  -H "Content-Type: application/json" \
  -d '{"weight":123.45,"unit":"g","raw":123456,"stable":true,"timestamp":1749876543,"seq":1}'

# 查询数据
curl http://localhost:8000/api/v1/devices/esp32c3-test1234/records/latest
curl http://localhost:8000/api/v1/devices/esp32c3-test1234/records/stats

# MQTT 测试
mosquitto_pub -h localhost -t "espscale/esp32c3-test1234/weight" \
  -m '{"device_id":"esp32c3-test1234","weight":456.78,"unit":"g","stable":true,"timestamp":1749876543,"seq":2}' -q 1
```

## 待完成项

### 🟡 用户认证系统

- 添加 `POST /api/v1/auth/register` (用户名 + 密码 → 创建用户)
- 添加 `POST /api/v1/auth/login` (→ 返回 JWT)
- 设备管理端点添加 `Depends(get_current_user)` 保护
- WebSocket 连接添加 token 查询参数验证

### 🟡 读端点认证

- 设备列表、记录查询、统计、CSV 导出添加 JWT 认证
- 按用户过滤设备（需要 user_id 关联）

### 🟢 Alembic 迁移

- `alembic init migrations`
- 创建首次迁移 `alembic revision --autogenerate`
- 在 lifespan 中使用 `alembic upgrade head` 代替 `create_all`

### 🟢 Events 查询

- 添加 `GET /api/v1/devices/{id}/events` 端点 (使用已有的 `EventResponse` schema)

### 🟢 命令转发

- 添加 `POST /api/v1/devices/{id}/command` 端点
- 通过 MQTT 发布到 `espscale/{id}/cmd` topic (需要设备支持 MQTT cmd 订阅)
