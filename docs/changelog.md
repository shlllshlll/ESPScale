# ESPScale 变更记录

> 按版本倒序记录重大变更。格式：`YYYY-MM-DD` + 版本号 + 分类条目。

---

## v0.3.0 — 2026-07-12

**主题：统一鉴权 + MQTT 完善 + 数据库保留策略 + 默认 BLE_ONLY**

### 摘要

| 领域 | 变更 |
|------|------|
| 鉴权 | 统一 API Key（设备 `X-Device-ID` + `X-API-Key`，APP `X-API-Key`） |
| MQTT | 进程内 amqtt Broker（替代 Mosquitto）；Arduino/ESP-IDF 完整客户端 |
| 数据 | 可配置保留天数 / 每设备最大记录数 / VACUUM / WAL |
| 密钥 | `secrets/`（gitignore）+ `secrets.template/` 模板体系 |
| 默认模式 | `DEFAULT_MODE = MODE_BLE_ONLY (2)`，WiFi 凭证仍存 NVS |

---

### A. 服务端鉴权

**统一方案：**

| 调用方 | 方式 | 说明 |
|--------|------|------|
| 固件 → 服务器 | `X-Device-ID` + `X-API-Key` | 设备级 API Key（注册时生成，SHA256 存库） |
| APP → 服务器 | `X-API-Key` | 共享 `APP_API_KEY`（环境变量） |
| 开发模式 | `APP_API_KEY` 为空 | 鉴权关闭，便于本地调试 |

**改动文件：**

| 文件 | 变更 |
|------|------|
| `server/deps.py` | 重写：`get_app_auth` / `get_device_auth` |
| `server/routes/devices.py` | 全部 5 端点受 `get_app_auth` 保护；删除时级联 |
| `server/routes/data.py` | 写入用 `get_device_auth`，读取用 `get_app_auth` |
| `server/routes/ws.py` | WebSocket 通过查询参数 `?api_key=` 校验 |
| `server/mqtt_bridge.py` | 处理消息前校验设备是否已注册；MQTT 连接使用 `MQTT_USER`/`MQTT_PASS` |
| `server/config.py` | 新增 `APP_API_KEY`、`MQTT_USER`、`MQTT_PASS` |

---

### B. APP 鉴权 + 模式切换

| 文件 | 变更 |
|------|------|
| `app/lib/config.dart` | `ServerConfig` 增加 `appApiKey` + `copyWith` |
| `app/lib/providers/app_providers.dart` | 持久化/加载 `app_api_key`；`setApiKey()` |
| `app/lib/services/api_service.dart` | Dio 请求头注入 `X-API-Key` |
| `app/lib/services/ws_service.dart` | 连接附加 `?api_key=` |
| `app/lib/screens/settings_screen.dart` | 服务器 API Key 编辑/清除对话框 |
| `app/lib/screens/device_detail_screen.dart` | WS 连接传递 apiKey |
| `app/lib/screens/device_settings_screen.dart` | 保存时通过 BLE 推送 mode 变更 |
| `app/lib/screens/provision_screen.dart` | 配网下发 `mqtt_user` / `mqtt_pass` |
| `app/lib/services/ble_service.dart` | `provisionDevice` 接受 `mqttUser`/`mqttPass` |
| `app/lib/generated/secrets.dart` | 由 `scripts/generate_app_secrets.dart` 生成（`kAppApiKey`） |

---

### C. Arduino 固件 MQTT 完善

| 文件 | 变更 |
|------|------|
| `firmware/src/mqtt_client.cpp` | 完整重写：`initiateConnect()`、LWT、online status、cmd 订阅、`commandDispatch` 回调；修复 `DISCONNECTED → CONNECTING` 空 break；补 `mqttCallback` 前向声明 |
| `firmware/src/mqtt_client.h` | 新增 `mqttClientConnect()` / `mqttClientReset()` |
| `firmware/src/command_dispatch.h/.cpp` | **新文件**：BLE 与 MQTT 共用命令分发 |
| `firmware/src/protocol.h/.cpp` | `protocolBuildAck` 增加可选 `const char* data` |
| `firmware/src/ble_protocol.cpp` | 改用 `commandDispatch`；上报真实 MQTT 连接状态 |
| `firmware/src/state_machine.cpp` | 进入 `CONNECTING_MQTT` 时调用 `mqttClientReset()` |
| `firmware/src/storage.h/.cpp` | 默认 mode = `DEFAULT_MODE`（BLE_ONLY） |
| `firmware/src/config.h` | 新增 `DEFAULT_MODE = MODE_BLE_ONLY` |
| `firmware/platformio.ini` | `-include ../secrets/firmware_arduino.h` |

**MQTT 行为：**

- Topic：`espscale/{id}/weight`、`status`（LWT + online）、`cmd`（远程命令）、`event`（ack）
- 连接失败指数退避重试
- 命令经 `commandDispatch` 与 BLE 路径一致

---

### C2. ESP-IDF 固件 MQTT 完善

| 文件 | 变更 |
|------|------|
| `firmware-espidf/components/mqtt_handler/mqtt_handler.c` | 从 stub 重写为完整 `esp_mqtt_client`（LWT / status / cmd / event） |
| `firmware-espidf/components/mqtt_handler/include/mqtt_handler.h` | 新增 start/stop/reset/publish_event |
| `firmware-espidf/components/protocol/*` | JSON 辅助函数改为公共：`protocol_json_get_string` / `protocol_json_get_number` |
| `firmware-espidf/components/ble_service/ble_service.c` | 报告 MQTT 状态；WiFi 凭证写入解析 `mqtt_user`/`mqtt_pass` |
| `firmware-espidf/src/main.c` | `network_task` 重写：BLE_ONLY 跳过 WiFi/MQTT；MQTT 等待/超时；模式变更热重载 |
| `firmware-espidf/src/main.c` scale_task | 命令处理增强：TARE / CALIBRATE / SET_MODE / SET_CONFIG / SET_WIFI / SET_MQTT / REBOOT / FACTORY_RESET |
| `firmware-espidf/src/config.h` | `DEFAULT_MODE = MODE_BLE_ONLY` |
| `firmware-espidf/components/storage/storage.c` | 默认 mode = `DEFAULT_MODE` |
| `firmware-espidf/platformio.ini` | `-include ../secrets/firmware_espidf.h` |

**CMake 修复（编译阻断）：** 以下组件补 `PRIV_INCLUDE_DIRS .../src` 以找到 `config.h`：

- `mqtt_handler`、`wifi_manager`、`storage`、`scale_sensor`、`http_client`

**`network_task` 行为：**

1. 周期性重载 NVS 配置，检测 mode 变更
2. `MODE_BLE_ONLY`：不连 WiFi/MQTT，状态 `RUNNING`
3. `MODE_HTTP_DIRECT` / `MODE_MQTT`：连 WiFi（超时 → `ERROR_WIFI`）
4. MQTT 模式：`mqtt_handler_start()` + 等待 `MQTT_CONNECTED_BIT`（超时 → `ERROR_MQTT`）
5. mode 变更时断开并重置 WiFi/MQTT 再按新模式重连

---

### D. 内嵌 MQTT（amqtt，替代 Mosquitto / Docker）

| 文件 | 变更 |
|------|------|
| `server/mqtt_broker.py` | **新文件**：amqtt 嵌入 Broker，`MQTT_USER`/`PASS` 启用 FileAuth |
| `server/mqtt_bridge.py` | 改用 amqtt `MQTTClient` 订阅（去掉 paho-mqtt） |
| `server/main.py` | lifespan：先 start broker，再 start bridge |
| 删除 | `docker-compose.yml`、`server/Dockerfile`、`mosquitto/` |

---

### E. 数据库保留策略

| 文件 | 变更 |
|------|------|
| `server/config.py` | 8 个环境变量（见下表） |
| `server/database.py` | SQLAlchemy 事件启用 WAL + foreign_keys PRAGMA |
| `server/models.py` | `WeightRecord` FK `ondelete="CASCADE"` |
| `server/retention.py` | **新文件**：清理过期记录、超限截断、VACUUM、`retention_loop` |
| `server/main.py` | lifespan 启动 `retention_task` |

**环境变量：**

| 变量 | 默认 | 说明 |
|------|------|------|
| `DATA_RETENTION_ENABLED` | `true` | 是否按天数清理 |
| `DATA_RETENTION_DAYS` | `90` | 保留天数 |
| `MAX_RECORDS_ENABLED` | `true` | 是否按条数截断 |
| `MAX_RECORDS_PER_DEVICE` | `100000` | 每设备最大记录数 |
| `DB_VACUUM_ENABLED` | `true` | 是否定期 VACUUM |
| `DB_VACUUM_INTERVAL_HOURS` | `24` | VACUUM 间隔 |
| `DB_CLEANUP_INTERVAL_HOURS` | `6` | 清理任务间隔 |
| `DB_WAL_MODE` | `true` | SQLite WAL 模式 |

---

### F. 密钥体系

```
secrets/                    # gitignore，真实密钥，禁止提交
├── server.env              # APP_API_KEY, SECRET_KEY, MQTT_USER, MQTT_PASS
├── firmware_arduino.h      # DEFAULT_MQTT_USER / DEFAULT_MQTT_PASS
├── firmware_espidf.h       # 同上
├── app_config.json         # app_api_key 等

secrets.template/           # 可提交模板
├── server.env
├── firmware_arduino.h
├── firmware_espidf.h
└── app_config.json

scripts/generate_app_secrets.dart
  → 读取 secrets/app_config.json
  → 生成 app/lib/generated/secrets.dart
```

**开发占位凭据（仅本地）：**

| 用途 | 用户 | 密码 |
|------|------|------|
| 内嵌 MQTT | `espscale` | `espscale_dev_pass` |

生产环境请用 `python3 -c "import os;print(os.urandom(16).hex())"` 等重新生成。

---

### G. 部署方式

- **已移除** Docker Compose / Dockerfile / 外置 Mosquitto
- 运行：`cd server && uvicorn main:app --host 0.0.0.0 --port 8000`
- 文档：`docs/environment.md`（开发 + 部署合一）

---

### 默认运行模式变更

| 项目 | 旧默认 | 新默认 |
|------|--------|--------|
| Arduino `storage` | `mode = 0` (HTTP) | `mode = DEFAULT_MODE` (BLE_ONLY=2) |
| ESP-IDF `config.h` / `storage` | HTTP | `DEFAULT_MODE = MODE_BLE_ONLY` |

WiFi / MQTT 凭证仍写入 NVS，切换到 HTTP/MQTT 模式时可直接复用，无需重新配网。

---

### H. 本迭代收尾（原「已知问题」已全部完成）

| 项 | 处理 |
|----|------|
| Arduino `mqttClientConnect` 死代码 | 状态机进入 `CONNECTING_MQTT` 时显式调用；`mqttClientTick` 的 DISCONNECTED 改为空闲等待，不再自动 connect |
| ESP-IDF / Arduino `DEFAULT_MQTT_USER`/`PASS` | NVS 无值时回退到 secrets 编译期宏；`config.h` 提供空串 fallback |
| Alembic | `server/alembic/` + `001_initial_schema`；`init_db()` 改为 `upgrade head`；旧库若表已存在则 `stamp head` |
| 生产占位密码 | 仍须人工替换 `secrets/server.env`（非代码项） |
| 部署 | 已改 amqtt 内嵌 + 删除 Docker/Mosquitto |

**Alembic 使用：**

```bash
cd server
# 生成新迁移（改 models 后）
.venv/bin/alembic revision --autogenerate -m "describe change"
# 手动升级
.venv/bin/alembic upgrade head
# 查看当前版本
.venv/bin/alembic current
```

启动服务时 `lifespan` → `init_db()` 会自动 upgrade。

---

## v0.2.0 — 既有迭代摘要

三端核心功能已实现。主要修复：

- APP 路由 `/settings` → `/device-settings`
- 配网下发真实 32 hex API Key
- 服务端 `register` 幂等更新 api_key
- 实时重量 BLE 优先，WS 后备
- `set_config` 支持 `api_key` 参数

详见仓库根目录 `CLAUDE.md` 与 `README.md`。
