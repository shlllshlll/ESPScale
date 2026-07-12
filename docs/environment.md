# 开发与部署指南

本文档覆盖 ESPScale **固件 / 服务端 / APP** 的环境搭建、日常开发命令与服务端部署。  
服务端为 **单进程 FastAPI**（内嵌 amqtt MQTT Broker），**不需要** Docker 或外置 Mosquitto。

---

## 1. 总览

```
固件 (MQTT) ──:1883──► amqtt（进程内）──► mqtt_bridge ──► SQLite / WebSocket
固件 (HTTP)  ──:8000──► FastAPI REST
APP          ──:8000──► REST + WebSocket
APP ↔ 固件   ── BLE GATT（配网 / 本地称重 / 命令）
```

| 端口 | 服务 |
|------|------|
| `8000` | HTTP / WebSocket / OpenAPI (`/docs`) |
| `1883` | 内嵌 MQTT（可关） |

| 工具 | 推荐版本 | 用途 |
|------|----------|------|
| Python | 3.11+ | 服务端 |
| PlatformIO CLI | 6.x | 固件编译烧录 |
| Flutter SDK | 3.44+ | APP |
| Git | 2.x | 版本管理 |
| Xcode 15+ | macOS/iOS | 可选 |
| Android Studio / SDK 33+ | Android | 可选 |

---

## 2. 硬件

- ESP32-C3-DevKitM-1 ×1
- HX711 称重模块 ×1
- 应变片 ×1 或 ×4（全桥）
- USB-C 数据线 ×1
- 杜邦线：`GPIO3→SCK`，`GPIO4→DOUT`，`3.3V→VCC`，`GND→GND`

```
ESP32-C3         HX711
────────────────────────
GPIO3  ──────── SCK
GPIO4  ──────── DOUT
3.3V   ──────── VCC
GND    ──────── GND
```

---

## 3. 密钥（首次）

```bash
# 在仓库根目录
cp -r secrets.template secrets   # 若尚未有 secrets/
# 编辑 secrets/server.env、firmware_*.h、app_config.json
```

`secrets/` 已 gitignore，**勿提交**。

开发占位（`secrets/server.env` 示例）：

| 项 | 值 |
|----|-----|
| MQTT 用户 | `espscale` |
| MQTT 密码 | `espscale_dev_pass` |
| `APP_API_KEY` | 空 = 关闭 APP 鉴权 |

生成随机密钥：

```bash
python3 -c "import os;print(os.urandom(16).hex())"
python3 -c "import os;print(os.urandom(32).hex())"
```

APP 密钥生成（可选）：

```bash
dart run scripts/generate_app_secrets.dart
# → app/lib/generated/secrets.dart
```

---

## 4. 固件 (firmware/)

### 4.1 安装 PlatformIO

```bash
pip3 install platformio
# 或
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py | python3

~/.platformio/penv/bin/pio --version
```

可选 alias：

```bash
alias pio='~/.platformio/penv/bin/pio'
```

### 4.2 编译 / 烧录 / 监视

```bash
cd firmware
~/.platformio/penv/bin/pio pkg install   # 首次
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -t upload
~/.platformio/penv/bin/pio device monitor --baud 115200
```

ESP-IDF 变体：`cd firmware-espidf`，命令相同。

### 4.3 主要库依赖

| 库 | 用途 |
|----|------|
| NimBLE-Arduino | BLE |
| HX711 | 称重 |
| PubSubClient | MQTT 客户端 |
| ArduinoJson | JSON |

固件 MQTT/HTTP 对接见下文 [§5.6](#56-固件对接)。

---

## 5. 服务端 (server/)

### 5.1 安装

```bash
cd server
python3 -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

### 5.2 开发启动（热重载）

```bash
cd server
source .venv/bin/activate
set -a && source ../secrets/server.env && set +a   # 可选

uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

### 5.3 生产启动

```bash
cd server
source .venv/bin/activate
set -a && source ../secrets/server.env && set +a

# workers 必须为 1（内嵌 MQTT 与进程内状态）
uvicorn main:app --host 0.0.0.0 --port 8000 --workers 1
```

最小示例（不加载 secrets 文件）：

```bash
cd server && source .venv/bin/activate
export MQTT_USER=espscale MQTT_PASS=espscale_dev_pass
uvicorn main:app --host 0.0.0.0 --port 8000
```

启动成功：

- API：http://localhost:8000/docs  
- MQTT：`mqtt://localhost:1883`（配置了用户密码则需认证）

### 5.4 环境变量

| 变量 | 默认 | 说明 |
|------|------|------|
| `DATABASE_URL` | `sqlite+aiosqlite:///espscale.db` | SQLite 路径 |
| `SECRET_KEY` | dev 占位 | 签名密钥 |
| `APP_API_KEY` | 空 | APP `X-API-Key`；空则关闭 APP 鉴权 |
| `MQTT_ENABLED` | `true` | 内嵌 Broker + Bridge |
| `MQTT_BIND` | `0.0.0.0` | Broker 监听地址 |
| `MQTT_PORT` | `1883` | Broker 端口 |
| `MQTT_BROKER_HOST` | `127.0.0.1` | Bridge 连本机 Broker |
| `MQTT_USER` / `MQTT_PASS` | 空 | 非空启用 MQTT 认证；空则匿名 |
| `DATA_RETENTION_ENABLED` | `true` | 按天数清理 |
| `DATA_RETENTION_DAYS` | `90` | 保留天数 |
| `MAX_RECORDS_ENABLED` | `true` | 按条数截断 |
| `MAX_RECORDS_PER_DEVICE` | `100000` | 每设备上限 |
| `DB_VACUUM_ENABLED` | `true` | 定期 VACUUM |
| `DB_VACUUM_INTERVAL_HOURS` | `24` | VACUUM 间隔 |
| `DB_CLEANUP_INTERVAL_HOURS` | `6` | 清理任务间隔 |
| `DB_WAL_MODE` | `true` | SQLite WAL |

仅 HTTP / BLE、不需要 MQTT：

```bash
export MQTT_ENABLED=false
uvicorn main:app --host 0.0.0.0 --port 8000
```

### 5.5 数据库迁移（Alembic）

启动时 `init_db()` 会自动 `upgrade head`。手动：

```bash
cd server && source .venv/bin/activate
alembic upgrade head
alembic current
# 修改 models 后：
alembic revision --autogenerate -m "describe change"
alembic upgrade head
```

### 5.6 固件对接

| 模式 | 配置 |
|------|------|
| **MQTT** | `mqtt_host` = 服务器 IP，`mqtt_port` = `1883`，`mqtt_user`/`mqtt_pass` 与 `server.env` 一致 |
| **HTTP** | `server_url` = `http://<服务器IP>:8000`，设备 API Key 写入 NVS |
| **BLE_ONLY** | 可不连服务器，本地 GATT 推流 |

### 5.7 健康检查

```bash
curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8000/docs
# 期望 200；服务端日志含 Embedded MQTT broker（若 MQTT 开启）
```

### 5.8 生产部署检查清单

1. 强随机 `SECRET_KEY`、`APP_API_KEY`、`MQTT_PASS`
2. `uvicorn ... --workers 1`
3. 反向代理 HTTPS（Caddy / Nginx）到 `8000`
4. 防火墙按需开放 `8000` / `1883`
5. 备份 `espscale.db`（或 `DATABASE_URL` 指向的路径）

### 5.9 常见问题

| 现象 | 处理 |
|------|------|
| `1883` 被占用 | `export MQTT_PORT=1884`，固件 `mqtt_port` 同步 |
| Bridge 连不上 | 确认 `MQTT_ENABLED=true`；`MQTT_BROKER_HOST=127.0.0.1`；账号与固件一致 |
| 多 worker 异常 | 必须 `--workers 1` |

---

## 6. APP (app/)

### 6.1 安装 Flutter

**macOS:**

```bash
git clone https://github.com/flutter/flutter.git -b stable ~/flutter
export PATH="$PATH:$HOME/flutter/bin"   # 写入 ~/.zshrc
flutter doctor
```

**Linux:**

```bash
sudo snap install flutter --classic
# 或 https://docs.flutter.dev/get-started/install/linux
flutter doctor
```

**Windows:** 解压 Flutter SDK 到 `C:\flutter`，将 `C:\flutter\bin` 加入 PATH，再 `flutter doctor`。

### 6.2 平台依赖

| 平台 | 额外依赖 |
|------|----------|
| macOS | Xcode 15+、CocoaPods |
| iOS | Xcode、模拟器/真机、开发者账号 |
| Android | Android Studio、SDK 33+、Java 17 |
| Windows | VS 2022 + C++ 桌面开发 |
| Linux | `clang cmake ninja-build pkg-config libgtk-3-dev libbluetooth-dev` |

Android 工具链补齐（macOS 示例）：

```bash
brew install --cask android-studio   # 或 android-commandlinetools
export ANDROID_HOME=$HOME/Library/Android/sdk
export PATH=$PATH:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools
flutter doctor --android-licenses
flutter doctor
```

### 6.3 运行

```bash
cd app
flutter pub get
flutter devices
flutter run -d macos          # 或 android / ios / ...
```

> 模型为手写 `fromJson`/`toJson`，**无需** `build_runner` / freezed。

服务器地址与 API Key 在 APP「设置」中配置；与 `APP_API_KEY` 一致。

---

## 7. 验证清单

1. **固件**
   ```bash
   cd firmware && ~/.platformio/penv/bin/pio run
   ```
   期望：`SUCCESS`，RAM &lt; 50%，Flash &lt; 90%

2. **服务端**
   ```bash
   cd server && source .venv/bin/activate && uvicorn main:app --reload --host 0.0.0.0 --port 8000
   ```
   打开 http://localhost:8000/docs ；日志有 Embedded MQTT broker（若启用）

3. **APP**
   ```bash
   flutter doctor
   cd app && flutter run -d macos
   ```
   应进入设备列表

4. **端到端（可选）**
   - 保持服务端运行
   - 烧录固件 → 串口见 PROVISIONING
   - APP：BLE 扫描 → 配网（WiFi + server_url / mqtt）→ 详情页重量更新
   - HTTP：串口 `HTTP POST OK`；MQTT：bridge 日志收到 `espscale/{id}/weight`

---

## 8. 相关文档

| 文档 | 内容 |
|------|------|
| [build.md](build.md) | 多端编译、打包、发布细节 |
| [firmware.md](firmware.md) | 固件协议与状态机 |
| [changelog.md](changelog.md) | 版本变更记录 |
