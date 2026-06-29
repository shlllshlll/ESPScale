# 开发环境搭建

本文档涵盖 ESPScale 三个模块（固件、服务端、APP）的开发环境配置。

## 硬件要求

- ESP32-C3-DevKitM-1 开发板 ×1
- HX711 称重传感器模块 ×1
- 称重传感器（应变片）×1 或 ×4（全桥）
- USB-C 数据线 ×1（用于烧录和串口监视）
- 杜邦线若干（GPIO3→SCK, GPIO4→DOUT, 3.3V→VCC, GND→GND）

## 软件要求

| 工具 | 推荐版本 | 说明 |
|------|----------|------|
| Python | 3.11+ | 服务端运行 + 脚本 |
| PlatformIO CLI | 6.x | 固件编译烧录 |
| Docker + Docker Compose | 24+ | 服务端部署 |
| Flutter SDK | 3.44+ (稳定版) | APP 编译（实测 3.44.4） |
| Git | 2.x | 版本管理 |
| Android Studio / SDK | API 33+ | Android 编译（可选） |
| Xcode | 15+ | iOS / macOS 编译（仅 macOS） |

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
~/.platformio/penv/bin/pio pkg install

# 编译
~/.platformio/penv/bin/pio run

# 烧录到 ESP32-C3 (通过 USB)
~/.platformio/penv/bin/pio run -t upload

# 串口监视 (115200 baud)
~/.platformio/penv/bin/pio device monitor
```

### 依赖 (platformio.ini)

| 库 | 用途 |
|------|------|
| h2zero/NimBLE-Arduino | BLE 协议栈 (省内存) |
| bogde/HX711 | HX711 称重传感器 |
| knolleary/PubSubClient | MQTT 客户端 |
| bblanchon/ArduinoJson | JSON 解析 |

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

# 停止并删除数据卷
docker compose down -v
```

服务端口：
- Mosquitto MQTT: `1883`
- Mosquitto WebSocket (可选): `9001`
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
| **iOS** | Xcode 15+, iOS Simulator 或真机 + Apple Developer 账号 |
| **Android** | Android Studio, Android SDK 33+, Android cmdline-tools, Java 17 |
| **Windows** | Visual Studio 2022 + "Desktop development with C++" |
| **Linux** | `sudo apt install clang cmake ninja-build pkg-config libgtk-3-dev libbluetooth-dev` |

### Android 工具链修复

如果 `flutter doctor` 提示 Android toolchain 缺失:

```bash
# 1. 安装 Android Studio (推荐)
brew install --cask android-studio

# 2. 或只装命令行工具
brew install --cask android-commandlinetools

# 3. 设置环境变量 (~/.zshrc)
export ANDROID_HOME=$HOME/Library/Android/sdk
export PATH=$PATH:$ANDROID_HOME/cmdline-tools/latest/bin
export PATH=$PATH:$ANDROID_HOME/platform-tools

# 4. 接受许可
flutter doctor --android-licenses

# 5. 重新检查
flutter doctor
```

### 安装依赖 + 运行

```bash
cd app

# 安装 Dart 包
flutter pub get

# 列出可用设备
flutter devices

# 启动 (macOS 示例)
flutter run -d macos
```

> **注意**: 本项目**没有**使用 freezed / json_serializable 代码生成，
> 模型序列化全部手写实现 (`fromJson` / `toJson`)，无需运行 `build_runner`。

## 验证清单

完成环境搭建后，按顺序验证：

1. **固件**：
   ```bash
   cd firmware && ~/.platformio/penv/bin/pio run
   ```
   期望：`[SUCCESS] Took X seconds`，RAM < 50%，Flash < 90%

2. **服务端**：
   ```bash
   cd server && uvicorn main:app --reload
   ```
   访问 `http://localhost:8000/docs` 应看到 OpenAPI 文档

3. **APP**：
   ```bash
   flutter doctor    # 期望全部绿色 (Chrome 可选)
   cd app && flutter run -d macos
   ```
   启动后应能看到设备列表页面

4. **端到端**（可选）：
   - 启动 `docker compose up -d`
   - 烧录固件，确认串口输出 PROVISIONING 状态
   - APP 扫描 BLE → 配网 → 验证 HTTP POST 出现在服务端日志
