# 多端编译与发布

本文档涵盖 ESPScale 三个模块的编译、打包与部署流程。

---

## 固件 (firmware/) — ESP32-C3

### 编译

```bash
cd firmware
~/.platformio/penv/bin/pio run
```

编译产物位于 `.pio/build/esp32-c3-devkitm-1/`：
- `firmware.elf` — ELF 可执行文件
- `firmware.bin` — 烧录镜像
- `firmware.merged.bin` — 完整 Flash 镜像 (bootloader + partition table + firmware)

### 烧录

```bash
# USB 连接 ESP32-C3，自动检测端口
~/.platformio/penv/bin/pio run -t upload

# 指定端口
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem*

# 擦除 Flash 后烧录 (首次或固件损坏时)
~/.platformio/penv/bin/pio run -t erase -t upload
```

### 串口监视

```bash
~/.platformio/penv/bin/pio device monitor --baud 115200
```

### 生产批量烧录 (esptool.py)

```bash
# 导出完整 Flash 镜像
esptool.py \
  --chip esp32c3 \
  merge_bin -o firmware_merged.bin \
  --flash_mode dio --flash_size 4MB \
  0x0 .pio/build/esp32-c3-devkitm-1/bootloader.bin \
  0x8000 .pio/build/esp32-c3-devkitm-1/partitions.bin \
  0x10000 .pio/build/esp32-c3-devkitm-1/firmware.bin

# 烧录合并镜像
esptool.py --chip esp32c3 write_flash 0x0 firmware_merged.bin
```

### 资源占用 (实测)

| 资源 | 占用 | 总量 | 剩余 |
|------|------|------|------|
| RAM | 45,972 B (~14%) | 327,680 B | ~280 KB |
| Flash | 963,616 B (~73%) | 1,310,720 B | ~340 KB |

---

## 服务端 (server/) — Docker

### 开发运行

```bash
cd server
pip install -r requirements.txt

# 环境变量 (可选)
export DATABASE_URL=sqlite+aiosqlite:///espscale.db
export MQTT_BROKER_HOST=localhost
export MQTT_BROKER_PORT=1883
export SECRET_KEY=your-secret-here

uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

### Docker Compose 部署 (推荐)

```bash
# 项目根目录
docker compose up -d

# 查看服务状态
docker compose ps

# 查看日志
docker compose logs -f server
docker compose logs -f mosquitto

# 更新部署
docker compose build server
docker compose up -d --force-recreate server

# 完全清理 (含数据卷)
docker compose down -v
```

服务端口：
- FastAPI: `http://localhost:8000`
- API 文档: `http://localhost:8000/docs`
- Mosquitto MQTT: `1883`
- Mosquitto WebSocket: `9001` (可选)

### 生产环境注意事项

1. **必须**修改 `docker-compose.yml` 中的 `SECRET_KEY` (JWT 签名密钥)
2. Mosquitto 建议添加 `password_file` 认证配置
3. SQLite 数据文件 `./data/espscale.db` 通过卷挂载持久化
4. 加上 Nginx / Caddy 反向代理终止 HTTPS
5. 当前 `register` 端点已支持幂等更新 (重复配网可更新 api_key)

---

## APP (app/) — Flutter

### 平台支持状态

| 平台 | 状态 | 说明 |
|------|------|------|
| **macOS** | ✅ 完整 | 主力开发/调试平台 |
| **iOS** | ✅ 完整 | 蓝牙 + WebSocket |
| **Android** | ✅ 完整 | 需要 Android SDK + 模拟器/真机 |
| **Windows** | ⚠️ 蓝牙受限 | Win10+ 通常可用 WinRT BLE |
| **Linux** | ⚠️ 蓝牙受限 | 需要 BlueZ 5.x + 手动权限 |
| **Web** | ❌ 不支持 | flutter_blue_plus 不支持 Web 平台 |

### 通用步骤

```bash
cd app
flutter pub get
```

> 注：项目**没有**使用 freezed / json_serializable 代码生成（已切换为手写 fromJson/toJson），
> 因此无需运行 `build_runner`。

### macOS 桌面版

```bash
flutter run -d macos

# Release 打包
flutter build macos --release
# 产物: build/macos/Build/Products/Release/ESPScale.app
```

### iOS

```bash
# 模拟器
flutter run -d ios

# 真机 (需 Apple Developer 账号 + 配置文件)
flutter run -d ios --release

# 打包 (Xcode Archive)
flutter build ios --release
# 在 Xcode 中: Product > Archive > Distribute App
```

> 蓝牙权限说明: iOS 13+ 需要在 `ios/Runner/Info.plist` 中声明
> `NSBluetoothAlwaysUsageDescription`。

### Android

```bash
# 模拟器
flutter run -d android

# 真机
flutter run -d android --release

# 编译 APK (包含所有 ABI)
flutter build apk --release
# 产物: build/app/outputs/flutter-apk/app-release.apk

# 编译分架构 APK (更小，推荐)
flutter build apk --release --split-per-abi
# 产物:
#   app-arm64-v8a-release.apk
#   app-armeabi-v7a-release.apk
#   app-x86_64-release.apk

# 编译 AAB (Google Play 上架)
flutter build appbundle --release
# 产物: build/app/outputs/bundle/release/app-release.aab
```

#### 安装到真机

```bash
# USB 连接 Android 手机，开启「开发者选项 → USB 调试」
adb devices

# 安装
adb install build/app/outputs/flutter-apk/app-release.apk

# 覆盖安装
adb install -r build/app/outputs/flutter-apk/app-release.apk

# 指定设备
adb -s <device-id> install build/app/outputs/flutter-apk/app-release.apk
```

#### Android 权限

`flutter_blue_plus` 需要以下权限（首次启动 APP 时会请求）:
- `BLUETOOTH_SCAN` (Android 12+)
- `BLUETOOTH_CONNECT` (Android 12+)
- `ACCESS_FINE_LOCATION` (Android 6-11，蓝牙扫描需要)

#### Android 签名

当前 `android/app/build.gradle.kts` 用 debug 签名仅供测试。
上架 Google Play 需要生成自己的 keystore:

```bash
keytool -genkey -v -keystore ~/upload-keystore.jks \
  -keyalg RSA -keysize 2048 -validity 10000 -alias upload
```

然后在 `android/key.properties` 配置密钥信息，并在 `build.gradle.kts` 引用。

### Windows 桌面版

```powershell
flutter run -d windows

# Release 打包
flutter build windows --release
# 产物: build/windows/x64/runner/Release/ESPScale.exe
```

### Linux 桌面版

```bash
flutter run -d linux

# Release 打包
flutter build linux --release
# 产物: build/linux/x64/release/bundle/ESPScale
```

### 测试

```bash
# 单元测试
flutter test

# 集成测试 (需要先创建 integration_test/ 目录)
flutter test integration_test/
```

---

## 端到端集成测试流程

1. **启动基础设施**
   ```bash
   docker compose up -d
   ```

2. **烧录固件** → ESP32-C3 启动 → 串口监视确认 PROVISIONING 状态
   ```bash
   cd firmware
   ~/.platformio/penv/bin/pio run -t upload
   ~/.platformio/penv/bin/pio device monitor
   ```

3. **APP 配网**
   ```bash
   cd app && flutter run -d macos
   ```
   - 切换到 "Local (BLE)" Tab → 扫描 → 发现 `ESPScale-XXXX`
   - 点 Connect → 跳到 Provision 页面
   - 读 Device Info → 选模式 (HTTP/MQTT/BLE) → 输入 WiFi/Server/MQTT
   - 点 "Provision" → 等待 Network Status NOTIFY → WiFi 连接成功
   - 配网成功后 → APP 自动注册到服务器 → 跳到设备详情

4. **验证三种模式**
   - **HTTP**: 串口应看到 `[INFO] HTTP POST OK`, 服务器应收到 `POST /api/v1/data`
   - **MQTT**: 服务器 MQTT bridge 应收到 `espscale/{id}/weight` 消息
   - **BLE Only**: 切到 BLE 模式后，APP 通过 Weight Stream 实时显示

5. **APP 详情页验证**
   - 实时重量数字更新 (BLE notify 或 WebSocket)
   - 折线图显示历史 (来自服务器 API)
   - 三个操作按钮: Tare / Calibrate / Record
   - 右上角 History 按钮可看本地保存的记录
