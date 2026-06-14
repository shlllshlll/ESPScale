# 开发环境搭建

本文档涵盖 ESPScale 三个模块（固件、服务端、APP）的开发环境配置。

## 硬件要求

- ESP32-C3-DevKitM-1 开发板 ×1
- HX711 称重传感器模块 ×1
- 称重传感器（应变片） ×1 或 ×4（全桥）
- USB-C 数据线 ×1（用于烧录和串口监视）
- 杜邦线若干（GPIO3→SCK, GPIO4→DOUT, 3.3V→VCC, GND→GND）

## 软件要求

| 工具 | 最低版本 | 说明 |
|------|----------|------|
| Python | 3.11+ | 服务端运行 + 脚本 |
| PlatformIO CLI | 6.x | 固件编译烧录 |
| Docker + Docker Compose | 24+ | 服务端部署 |
| Flutter | 3.22+ | APP 编译 |
| Git | 2.x | 版本管理 |

## 固件环境 (firmware/)

### PlatformIO 安装

```bash
# 方式 A: pip 安装 (推荐 macOS/Linux)
pip3 install platformio

# 方式 B: 安装脚本
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py | python3

# 验证
~/.platformio/penv/bin/pio --version
```

### 可选：配置 alias

```bash
# 添加到 ~/.zshrc 或 ~/.bashrc
alias pio='~/.platformio/penv/bin/pio'
```

### 编译与烧录

```bash
cd firmware

# 安装依赖 (首次)
pio pkg install

# 编译
pio run

# 烧录到 ESP32-C3 (通过 USB)
pio run -t upload

# 串口监视 (115200 baud)
pio device monitor
```

### 依赖 (platformio.ini)

```
h2zero/NimBLE-Arduino   # BLE 协议栈
bogde/HX711             # HX711 称重传感器
knolleary/PubSubClient  # MQTT (Phase 2)
bblanchon/ArduinoJson   # JSON 解析
```

### 接线参考

```
ESP32-C3         HX711
────────────────────────
GPIO3  ──────── SCK
GPIO4  ──────── DOUT
3.3V  ──────── VCC
GND   ──────── GND
```

## 服务端环境 (server/)

### Python 虚拟环境（推荐）

```bash
python3 -m venv venv
source venv/bin/activate
```

### 安装依赖

```bash
cd server
pip install -r requirements.txt
```

### 本地运行（开发）

```bash
# 直接运行 (需先启动 Mosquitto)
uvicorn main:app --reload --host 0.0.0.0 --port 8000

# 访问自动生成的 API 文档
open http://localhost:8000/docs
```

### Mosquitto MQTT Broker

```bash
# 方式 A: 本地安装
brew install mosquitto          # macOS
sudo apt install mosquitto      # Ubuntu/Debian

# 启动
mosquitto -c mosquitto/config/mosquitto.conf

# 方式 B: Docker
docker run -d --name mosquitto -p 1883:1883 \
  -v $(pwd)/mosquitto/config:/mosquitto/config \
  eclipse-mosquitto:2
```

### Docker Compose 一键部署

```bash
# 根目录
docker compose up -d

# 查看日志
docker compose logs -f

# 停止
docker compose down
```

服务端口：
- Mosquitto MQTT: `1883`
- FastAPI 服务: `8000`
- API 文档: `http://localhost:8000/docs`

## APP 环境 (app/)

### Flutter SDK 安装

**macOS:**

```bash
# 下载 Flutter SDK
cd ~
git clone https://github.com/flutter/flutter.git -b stable

# 添加到 PATH (~/.zshrc)
export PATH="$PATH:$HOME/flutter/bin"

# 验证
flutter doctor
```

**Linux:**

```bash
sudo snap install flutter --classic
# 或手动下载: https://docs.flutter.dev/get-started/install/linux
flutter doctor
```

**Windows:**

```powershell
# 下载 Flutter SDK zip 并解压到 C:\flutter
# 添加 C:\flutter\bin 到 PATH
flutter doctor
```

### 平台前置要求

| 平台 | 额外依赖 |
|------|----------|
| **macOS** | Xcode 15+ (App Store), CocoaPods (`sudo gem install cocoapods`) |
| **iOS** | Xcode 15+, iOS Simulator |
| **Android** | Android Studio, Android SDK 33+, Android Emulator |
| **Windows** | Visual Studio 2022 + "Desktop development with C++" |
| **Linux** | `sudo apt install clang cmake ninja-build pkg-config libgtk-3-dev` |

### 安装依赖 + 运行

```bash
cd app

# 安装 Dart 包
flutter pub get

# 代码生成 (freezed + json_serializable)
flutter pub run build_runner build --delete-conflicting-outputs
```

## 验证清单

完成环境搭建后，按顺序验证：

1. **固件**：`cd firmware && pio run` 编译成功
2. **服务端**：`uvicorn main:app --reload` 启动，访问 `/docs` 可看到 API
3. **APP**：`flutter doctor` 全部绿色，`flutter run -d macos` 启动桌面版
