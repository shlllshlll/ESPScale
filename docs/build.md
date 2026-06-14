# 多端编译与发布

本文档涵盖 ESPScale 三个模块的编译、打包与部署。

---

## 固件 (firmware/) — ESP32-C3

### 编译

```bash
cd firmware
pio run
```

编译产物位于 `.pio/build/esp32-c3-devkitm-1/`：
- `firmware.elf` — ELF 可执行文件
- `firmware.bin` — 烧录镜像

### 烧录

```bash
# USB 连接 ESP32-C3，自动检测端口
pio run -t upload

# 指定端口
pio run -t upload --upload-port /dev/cu.usbmodem*

# 擦除 Flash 后烧录 (首次或固件损坏时)
pio run -t erase -t upload
```

### 串口监视

```bash
pio device monitor --baud 115200
```

### 生产批量烧录

```bash
# 导出完整 Flash 镜像（含 bootloader + partition table + firmware）
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

### Docker Compose 部署

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
```

### 生产环境注意事项

1. 修改 `docker-compose.yml` 中的 `SECRET_KEY`
2. Mosquitto 建议添加认证配置
3. SQLite 数据文件 `data/espscale.db` 外挂卷持久化
4. 加上 Nginx 反向代理终止 HTTPS

---

## APP (app/) — Flutter 6 平台

### 安装依赖

```bash
cd app
flutter pub get
```

### macOS 桌面版

```bash
flutter run -d macos

# Release 打包
flutter build macos --release
# 产物: build/macos/Build/Products/Release/ESPScale.app
```

### Windows 桌面版

```powershell
flutter run -d windows

# Release 打包
flutter build windows --release
# 产物: build/windows/x64/runner/Release/
```

### Linux 桌面版

```bash
flutter run -d linux

# Release 打包
flutter build linux --release
# 产物: build/linux/x64/release/bundle/
```

### iOS

```bash
# 模拟器
flutter run -d ios

# 真机 (需 Apple Developer 账号)
flutter run -d ios --release

# 打包 (Xcode Archive)
flutter build ios --release
# 在 Xcode 中: Product > Archive > Distribute App
```

### Android

```bash
# 模拟器
flutter run -d android

# 真机
flutter run -d android --release

# APK
flutter build apk --release
# 产物: build/app/outputs/flutter-apk/app-release.apk

# AAB (Google Play)
flutter build appbundle --release
# 产物: build/app/outputs/bundle/release/app-release.aab
```

### 代码生成

```bash
# 首次或模型变更后
flutter pub run build_runner build --delete-conflicting-outputs

# 开发时 watch 模式
flutter pub run build_runner watch --delete-conflicting-outputs
```

### 测试

```bash
# 单元测试
flutter test

# 集成测试
flutter test integration_test/
```

---

## 端到端集成测试

1. **启动基础设施**
   ```bash
   docker compose up -d
   ```

2. **烧录固件** → ESP32-C3 启动 → 串口监视确认 PROVISIONING 状态

3. **APP 配网**
   ```bash
   cd app && flutter run -d macos
   ```
   - 扫描 BLE → 发现 `ESPScale-XXXX`
   - 连接 → 读取 Device Info
   - 输入 WiFi SSID/密码 → 写入 WiFi Credentials
   - 等待 Network Status NOTIFY → WiFi 连接成功

4. **验证数据流**
   - 串口监视：`state=RUNNING`
   - 服务端 API：`GET /api/v1/devices/{id}/records` 有数据
   - APP 详情页：实时重量 + 图表显示
