# APP 端实施计划 (app/)

> 状态：Phase 4 待开始

## 目标

构建跨平台（iOS/Android/macOS/Windows/Linux）称重控制 APP，支持：

1. **BLE 配网**：扫描 → 连接 → 发送 WiFi 凭据
2. **本地称重**：BLE 实时推流，大字号显示，清零/校准
3. **远程监控**：通过服务端 API 查看历史数据和在线状态
4. **设备管理**：编辑设置，图表分析

## 技术选型

最终选择 **Flutter**（经 Tauri BLE 可行性评估后决策）：

| 项目 | 选型 | 原因 |
|------|------|------|
| 框架 | Flutter 3.x | 单代码库覆盖 6 平台 |
| BLE | flutter_blue_plus | 跨平台 BLE 最成熟（iOS/Android/macOS/Windows/Linux） |
| 状态管理 | Riverpod | 类型安全，编译时检查 |
| HTTP | dio | 拦截器/超时/重试 |
| 图表 | fl_chart | Flutter 最流行图表库 |
| WebSocket | web_socket_channel | 官方推荐 |
| 代码生成 | freezed + json_serializable | 不可变模型自动生成 |

## Tauri 可行性评估回顾

| 平台 | BLE 能力 | 结论 |
|------|----------|------|
| macOS | ✅ btleplug (CoreBluetooth) | 可行但需自定义 Rust 插件 (~600行) |
| Windows | ✅ btleplug (WinRT) | 可行 |
| Linux | ✅ btleplug (BlueZ) | 可行 |
| iOS | ❌ Tauri mobile 不成熟 | **不可行** |
| Android | ❌ 同上 | **不可行** |

结论：Flutter 是更合理的选择，`flutter_blue_plus` 已有成熟的多平台 BLE 支持。

## 项目结构

```
app/
├── pubspec.yaml
├── lib/
│   ├── main.dart                    # 入口, MaterialApp.router, 主题
│   ├── app.dart                     # App widget, GoRouter 路由配置
│   ├── config.dart                  # API base URL, 常量
│   │
│   ├── models/
│   │   ├── device.dart              # @freezed Device 模型
│   │   ├── weight_record.dart       # @freezed WeightRecord 模型
│   │   ├── ble_characteristics.dart # BLE 特征值 UUID 映射
│   │   └── command.dart             # 命令请求/响应模型
│   │
│   ├── services/
│   │   ├── api_service.dart         # dio HTTP REST 客户端
│   │   ├── ws_service.dart          # WebSocket 客户端 (web_socket_channel)
│   │   ├── ble_service.dart         # BLE 扫描/连接/读写/订阅 (flutter_blue_plus)
│   │   ├── auth_service.dart        # JWT token 存储/刷新
│   │   └── storage_service.dart     # 本地 SharedPreferences 封装
│   │
│   ├── providers/
│   │   ├── auth_provider.dart       # 用户认证状态
│   │   ├── device_list_provider.dart # 设备列表 (从 API + BLE 扫描)
│   │   ├── ble_connection_provider.dart # 当前 BLE 连接状态
│   │   ├── live_weight_provider.dart # 实时重量 (来自 BLE Notify 或 WS)
│   │   └── settings_provider.dart   # 设备设置状态
│   │
│   ├── screens/
│   │   ├── login_screen.dart        # 登录/注册
│   │   ├── device_list_screen.dart  # 设备列表 (本地 BLE + 远程列表)
│   │   ├── provision_screen.dart    # BLE 配网向导
│   │   ├── device_detail_screen.dart # 设备详情 (重量图表 + 实时显示)
│   │   ├── calibration_screen.dart  # 校准向导
│   │   ├── device_settings_screen.dart # 设备设置编辑
│   │   └── settings_screen.dart     # APP 全局设置
│   │
│   ├── widgets/
│   │   ├── weight_display.dart      # 大字号实时重量
│   │   ├── weight_chart.dart        # 时间序列折线图 (fl_chart)
│   │   ├── device_card.dart         # 设备列表项 (在线状态/最后重量)
│   │   ├── connection_indicator.dart # 连接状态指示
│   │   └── ble_scan_list.dart       # BLE 扫描结果列表
│   │
│   └── utils/
│       ├── formatters.dart          # 重量/日期格式化
│       └── validators.dart          # 表单验证
│
├── test/
├── assets/
│   └── icons/
├── android/
├── ios/
├── macos/
├── windows/
└── linux/
```

## BLE Service 设计

```dart
class BleService {
  /// 扫描附近的 ESPScale 设备 (按 Service UUID 过滤)
  Stream<ScanResult> scanForScales({
    Duration timeout = const Duration(seconds: 10),
  });

  /// 连接到设备
  Future<void> connect(String deviceId);

  /// 发现服务和特征值
  Future<void> discoverServices();

  /// 读取特征值 (返回解析后的 JSON Map)
  Future<Map<String, dynamic>> readCharacteristic(String uuid);

  /// 写入特征值 (data 自动 JSON 编码)
  Future<void> writeCharacteristic(String uuid, Map<String, dynamic> data);

  /// 订阅通知 (返回 Stream, 自动 JSON 解码)
  Stream<Map<String, dynamic>> notifyCharacteristic(String uuid);

  /// 完整配网流程
  Future<ProvisioningResult> provisionDevice({
    required String ssid,
    required String password,
    String? mqttHost,
    int? mqttPort,
  });

  /// 断开连接
  Future<void> disconnect();

  /// 获取连接状态
  Stream<BleConnectionState> get connectionState;
}
```

## BLE 配网流程

```
ProvisionScreen
  │
  ├── 1. 扫描 BLE 设备
  │     bleService.scanForScales()
  │     显示 Service UUID 匹配的设备列表
  │
  ├── 2. 用户选择设备 → 连接
  │     bleService.connect(deviceId)
  │
  ├── 3. 发现服务 → 读取 Device Info
  │     bleService.readCharacteristic(CHAR_DEVICE_INFO_UUID)
  │     显示: device_id, fw_version, mode
  │
  ├── 4. 用户输入 WiFi 凭据 (SSID + Password)
  │     可选: MQTT broker 地址
  │
  ├── 5. 写入 WiFi Credentials
  │     bleService.writeCharacteristic(CHAR_WIFI_CREDS_UUID, {
  │       "ssid": "...",
  │       "password": "..."
  │     })
  │
  ├── 6. 监听 Network Status (NOTIFY)
  │     bleService.notifyCharacteristic(CHAR_NETWORK_STATUS_UUID)
  │     等待: wifi.connected=true
  │     显示进度: WiFi 连接中 → MQTT 连接中 → 完成
  │
  └── 7. 配网完成 → 保存设备信息 → 跳转 DeviceDetailScreen
```

## 页面功能规格

### DeviceListScreen (首页)
- 两个 tab:
  - **本地设备**: 显示 BLE 扫描结果 + 已配对的附近设备
  - **远程设备**: 从服务端 API 获取已注册设备列表
- 每张 DeviceCard 显示:
  - 设备名称/ID
  - 在线状态指示 (绿点/灰点)
  - 最近一次重量 + 时间
- 点击进入 DeviceDetailScreen
- FAB: 添加设备 → ProvisionScreen

### ProvisionScreen (配网向导)
- Step 1: 扫描列表 → 选择设备
- Step 2: 连接设备 → 显示设备信息
- Step 3: 输入 WiFi SSID + 密码
- Step 4: 等待配网完成 (进度动画)
- Step 5: 成功 → 自动跳转详情页

### DeviceDetailScreen (设备详情)
- 顶部: 大字号实时重量 (来自 BLE Notify 或 WebSocket)
- 中部: 时间序列折线图 (fl_chart)
  - 时间选择器: 1h / 6h / 24h / 7d
  - 从 API 拉取历史数据
- 底部: 快捷操作按钮
  - 清零 (tare)
  - 切换单位 (g / kg / lb)
  - 切换模式 (local ↔ remote)
- AppBar 右侧: 设置齿轮 → DeviceSettingsScreen

### CalibrationScreen (校准向导)
- 引导式: "请清空秤盘 → 点击下一步"
- "放置已知重量 → 输入重量值 (如 500g) → 点击校准"
- 发送 `calibrate` 命令
- 显示新校准系数

### DeviceSettingsScreen (设备设置)
- 设备名称编辑
- 上传间隔 (slider: 1s, 2s, 5s, 10s, 30s, 60s)
- 重量单位选择 (g / kg / lb / oz)
- MQTT broker 配置
- 固件版本显示
- 恢复出厂设置 (危险操作，需二次确认)

## 状态管理 (Riverpod)

```dart
// 认证状态
final authProvider = StateNotifierProvider<AuthNotifier, AuthState>((ref) {
  return AuthNotifier(ref.read(authServiceProvider));
});

// 设备列表
final deviceListProvider = AsyncNotifierProvider<DeviceListNotifier, List<Device>>(
  DeviceListNotifier.new,
);

// BLE 连接
final bleConnectionProvider = StreamProvider<BleConnectionState>((ref) {
  return ref.read(bleServiceProvider).connectionState;
});

// 实时重量 (来自 BLE 或 WebSocket)
final liveWeightProvider = StreamProvider<WeightReading>((ref) {
  final source = ref.watch(weightSourceProvider); // 'ble' or 'ws'
  if (source == 'ble') {
    return ref.read(bleServiceProvider).notifyCharacteristic(CHAR_WEIGHT_STREAM_UUID)
      .map((json) => WeightReading.fromJson(json));
  } else {
    return ref.read(wsServiceProvider).weightStream
      .map((json) => WeightReading.fromJson(json));
  }
});
```

## 通信分层

```
┌─────────────────────────────┐
│  UI Layer (Screens/Widgets)  │
├─────────────────────────────┤
│  State Layer (Riverpod)      │
├─────────────────────────────┤
│  Service Layer               │
│  ┌──────────┬──────────────┐ │
│  │BleService│ApiService    │ │
│  │(BLE直连) │(HTTP+WS远程) │ │
│  └──────────┴──────────────┘ │
├─────────────────────────────┤
│  Physical Layer              │
│  [ESP32-C3]─BLE─/─WiFi─[Server]│
└─────────────────────────────┘
```

APP 根据设备连接方式自动选择数据通道：
- BLE 已连接 → 使用 BLE Weight Stream (本地模式)
- 设备在线 + BLE 未连接 → 使用 WebSocket (远程模式)
- 设备离线 → 显示最后的已知状态

## 实施任务

### Phase 4: APP 实现 (7-10 天)

1. 项目初始化
   - `flutter create app`
   - 配置 Material 3 主题
   - 添加依赖: `flutter_blue_plus`, `dio`, `riverpod`, `fl_chart`, `freezed`, `web_socket_channel`, `shared_preferences`, `go_router`

2. 数据模型
   - `models/device.dart` (freezed + json_serializable)
   - `models/weight_record.dart`
   - `models/ble_characteristics.dart` (UUID 映射)
   - `models/command.dart`

3. 通信服务
   - `services/ble_service.dart`: BLE 扫描/连接/读写/订阅
   - `services/api_service.dart`: dio HTTP 客户端 (interceptors, token refresh)
   - `services/ws_service.dart`: WebSocket (auto-reconnect)
   - `services/auth_service.dart`: JWT 管理 (SharedPreferences 存储)

4. 状态管理
   - Riverpod providers (auth, device list, ble, live weight, settings)

5. 页面实现
   - `screens/provision_screen.dart`: BLE 配网向导 (Stepper widget)
   - `screens/device_list_screen.dart`: 设备列表 + BLE 扫描
   - `screens/device_detail_screen.dart`: 实时重量 + 图表
   - `screens/calibration_screen.dart`: 校准向导
   - `screens/device_settings_screen.dart`: 设置表单
   - `screens/login_screen.dart`: 登录/注册

6. UI 组件
   - `widgets/weight_display.dart`: 大字号重量数字 + 单位切换动画
   - `widgets/weight_chart.dart`: fl_chart 时间序列
   - `widgets/device_card.dart`: 设备卡片
   - `widgets/ble_scan_list.dart`: BLE 扫描结果

7. 测试
   - BLE 配网流程端到端
   - 实时重量显示
   - 图表数据加载
   - 设备设置修改

## 验证方案

```bash
# 桌面端运行
cd app
flutter run -d macos        # macOS
flutter run -d windows      # Windows
flutter run -d linux        # Linux

# 移动端
flutter run -d ios          # iOS Simulator
flutter run -d android      # Android Emulator

# 测试
flutter test
```
