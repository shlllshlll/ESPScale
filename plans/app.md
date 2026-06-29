# APP 端实施计划 (app/)

> **状态：Phase 4 ✅ 已实现** | 版本 v0.2.0

## 目标

构建跨平台（iOS/Android/macOS/Windows/Linux）称重控制 APP，支持：

1. **BLE 配网**：扫描 → 连接 → 发送 WiFi 凭据 → 注册到服务器
2. **本地称重**：BLE 实时推流，大字号显示，清零/校准
3. **远程监控**：通过服务端 API 查看历史数据和在线状态
4. **设备管理**：编辑设置，图表分析
5. **本地记录**：用户主动记录重要测量值，离线可用

## 技术选型

| 项目 | 选型 | 状态 | 备注 |
|------|------|------|------|
| 框架 | Flutter 3.x | ✅ | Material 3, teal 主题, light/dark |
| BLE | flutter_blue_plus (v2.3.8) | ✅ | License.nonprofit |
| 状态管理 | Riverpod (v3.3.2) | ✅ | AsyncNotifier/StreamProvider/Notifier |
| HTTP | dio (v5.4.0) | ✅ | Provider 动态绑定 serverConfig |
| 图表 | fl_chart (v1.2.0) | ✅ | 曲线面积图 + 触摸提示 |
| WebSocket | web_socket_channel (v3.0.3) | ✅ | 指数退避重连 |
| 本地存储 | shared_preferences (v2.3.4) | ✅ | 服务器地址持久化 |
| 路由 | Navigator named routes | ✅ | 6 个命名路由 |
| 代码生成 | freezed + json_serializable | ❌ | 实际使用手写 fromJson/toJson |

## 项目结构 (实际)

```
app/lib/
├── main.dart                       ✅ Material 3 入口 + 6 路由
├── config.dart                     ✅ AppConfig + ServerConfig + BLE UUIDs
│
├── models/
│   ├── device.dart                 ✅ DeviceModel (手写序列化)
│   ├── weight_record.dart          ✅ WeightRecord + WeightStats
│   └── command.dart                ⚠️ CmdRequest + CmdResponse (已定义未使用)
│
├── services/
│   ├── api_service.dart            ✅ dio HTTP (8 methods)
│   ├── ble_service.dart            ✅ BLE 扫描/连接/配网/命令 (284 行)
│   └── ws_service.dart             ✅ WebSocket + 重连 (73 行)
│
├── providers/
│   └── app_providers.dart          ✅ 6 providers + WeightReading (157 行)
│
├── screens/
│   ├── device_list_screen.dart     ✅ 双 Tab (远程 + BLE) + Server 入口
│   ├── provision_screen.dart       ✅ 4 步配网向导 (凭据记忆 + 设备预读 + API key 生成)
│   ├── device_detail_screen.dart   ✅ 实时称重 (BLE 优先) + 图表 + Record + History
│   ├── calibration_screen.dart     ⚠️ 校准向导 (去皮逻辑有误)
│   ├── device_settings_screen.dart ✅ 设备设置 (HTTP server_url + MQTT broker:port)
│   ├── settings_screen.dart        ✅ APP 设置 (Server URL)
│   └── history_screen.dart         ✅ 本地记录历史列表 (新增)
│
└── widgets/
    ├── weight_display.dart         ✅ 大字号重量 (64px) (62 行)
    ├── weight_chart.dart           ✅ fl_chart 折线图 (70 行)
    ├── device_card.dart            ✅ 设备卡片 + 相对时间 (70 行)
    ├── connection_indicator.dart   ✅ BLE 连接指示 (37 行)
    └── ble_scan_list.dart          ✅ BLE 扫描列表 (33 行)
```

## 路由配置

| 路由 | 页面 | 状态 |
|------|------|------|
| `/devices` | DeviceListScreen | ✅ |
| `/provision` | ProvisionScreen | ✅ |
| `/device` | DeviceDetailScreen | ✅ |
| `/calibrate` | CalibrationScreen | ✅ |
| `/device-settings` | DeviceSettingsScreen | ✅ |
| `/app-settings` | AppSettingsScreen | ✅ |
| `/history` | HistoryScreen | ✅ (新增) |

## BLE Service 实现 (已实现)

```dart
class BleService {
  // ✅ 已实现的核心方法:
  Stream<ScanResult> scanForScales({Duration timeout});  // 按 Service UUID 过滤
  Future<void> connect(BluetoothDevice device);
  Future<Map<String, dynamic>> readCharacteristic(String uuid);
  Future<void> writeCharacteristic(String uuid, Map<String, dynamic> data);
  Future<Stream<Map<String, dynamic>>> notifyCharacteristic(String uuid);
  Future<ProvisionResult> provisionDevice({...});  // 完整配网流程
  Future<void> sendCommand(String cmd, Map params);
  Future<void> disconnect();

  // Streams:
  Stream<BleConnectionState> get connectionState;  // BroadcastStream
}
```

### 配网流程 (已实现)

```
provisionDevice():
  1. 读取 Device Info → 获取 device_id, fw_version, mode
  2. 写入 WiFi Credentials (含扩展字段: mode, server_url, mqtt)
  3. 订阅 Network Status notifications + 每 2s 轮询作为后备
  4. 等待 wifi.connected=true (超时 30s)
  5. 配网成功 → 发送 set_config 命令确认
```

## Riverpod Providers (已实现)

| Provider | 类型 | 说明 | 状态 |
|----------|------|------|------|
| `serverConfigProvider` | AsyncNotifier | SharedPreferences 持久化服务器地址 | ✅ |
| `deviceListProvider` | AsyncNotifier | 从 API 获取设备列表 | ✅ |
| `bleConnectionProvider` | StreamProvider | BLE 连接状态流 | ✅ |
| `bleScanResultsProvider` | Notifier | BLE 扫描结果 | ✅ |
| `weightSourceProvider` | Notifier | 当前数据源 (ble/ws/none) | ✅ |
| `liveWeightProvider` | StreamProvider | 统一重量流 (BLE 或 WS) | ✅ |

## 页面功能实现状态

### DeviceListScreen ✅
- 双 Tab: "远程" (API 设备列表 + 下拉刷新) + "本地 BLE" (自动扫描)
- BLE 面板: 蓝牙状态检测 (unauthorized/off/unknown)，BT 开启时自动扫描
- DeviceCard: 名称/ID/最后重量/在线状态/模式标签/相对时间
- FAB: 添加设备 → ProvisionScreen

### ProvisionScreen ✅
- Step 0: 读取设备信息 (device_id, fw_version, mode)
- Step 1: 输入 WiFi SSID + 密码 + 选择模式
  - HTTP Direct: 显示 server URL 配置
  - MQTT: 显示 broker + port 配置
  - BLE Only: 显示信息提示
- Step 2: 配网进行中 (进度动画 + 状态日志)
- Step 3: 成功 → 注册到服务器 → 跳转详情页
- ⚠️ 注册时使用硬编码 `'placeholder-key'` 作为 API Key

### DeviceDetailScreen ✅
- 双数据源: 优先服务器 API，回退 BLE 直连
- 实时重量: 根据模式选择 WebSocket 或 BLE Notify
- 历史图表: fl_chart 折线图 (服务器有数据时)
- 操作按钮: 清零 (tare), 校准 (跳转校准页), 模式切换
- AppBar: 连接指示器 + ⚠️ 设置按钮 (路由名错误)

### CalibrationScreen ⚠️
- Step 1: "清空秤盘" → 按钮文字 "Send Tare" 但实际调用 `_calibrate()` 而非 tare
- Step 2: "放置已知重量" → 输入值 → 发送 calibrate 命令
- ⚠️ 发送后立即显示 "校准完成" 而未等待设备确认
- ⚠️ 无 tare 命令实际发送

### DeviceSettingsScreen ⚠️
- 编辑: 名称, 单位, 上传间隔, 模式 (HTTP/MQTT/BLE), 服务器 URL
- 保存: PUT API + 刷新设备列表
- 🔴 `DropdownButtonFormField(initialValue:)` 编译错误，应为 `value:`
- ⚠️ 仅通过 API 工作，无 BLE 回退

### AppSettingsScreen ⚠️
- 服务器地址: 编辑对话框 + 重置到默认值
- 版本信息 + 默认服务器显示
- ⚠️ "清除所有数据" 按钮 `onTap: () {}` 空实现

## 通信架构 (已实现)

```
┌─────────────────────────────┐
│  UI Layer (Screens/Widgets)  │
├─────────────────────────────┤
│  State Layer (Riverpod)      │
│  6 providers                 │
├─────────────────────────────┤
│  Service Layer               │
│  ┌──────────┬──────────────┐ │
│  │BleService│ApiService    │ │
│  │(BLE直连) │(HTTP+WS远程) │ │
│  └──────────┴──────────────┘ │
├─────────────────────────────┤
│  Physical Layer              │
│  [ESP32-C3]─BLE─/─WiFi─[Server] │
└─────────────────────────────┘
```

APP 自动选择数据通道:
- BLE 已连接 → 使用 BLE Weight Stream ✅
- 设备在线 + 远程模式 → 使用 WebSocket ✅
- 设备离线 → 显示最后已知状态 ✅

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

## 待修复项

### 🔴 严重: 路由名不匹配

**位置**: `device_detail_screen.dart` AppBar 设置按钮
**问题**: 导航到 `'/settings'` 但注册路由为 `'/device-settings'`
**修复**: 改为 `Navigator.pushNamed(context, '/device-settings', arguments: widget.deviceId)`

### 🔴 严重: DropdownButtonFormField 编译错误

**位置**: `device_settings_screen.dart`
**问题**: `DropdownButtonFormField(initialValue: ...)` 参数不存在
**修复**: 改为 `DropdownButtonFormField(value: ...)`

### 🟡 校准页面去皮逻辑

**位置**: `calibration_screen.dart` Step 1
**问题**: "Send Tare" 按钮调用 `_calibrate()` 而非发送 `tare` 命令
**修复**: Step 1 发送 `{"cmd": "tare"}` 命令，Step 2 发送 calibrate

### 🟡 Mode 表示不一致

**问题**: 多处使用不同的 mode 表示:
- `provision_screen.dart`: 整数 0/1/2
- `device_settings_screen.dart`: 字符串 'http_direct'/'mqtt'/'ble_only'
- `device_detail_screen.dart`: 字符串 'local'/'remote'
- `device_card.dart`: 检查 `device.mode == 'local'`
**修复**: 全局统一为整数 0 (HTTP_DIRECT) / 1 (MQTT) / 2 (BLE_ONLY)，与固件 `config.h` 一致

### 🟡 硬编码 API Key

**位置**: `provision_screen.dart` `_finish()` 方法
**问题**: 设备注册使用 `'placeholder-key'`
**修复**: 从服务器注册响应中获取生成的 API Key

### 🟡 清除数据未实现

**位置**: `settings_screen.dart`
**问题**: "清除所有数据" 按钮 `onTap` 为空
**修复**: 添加 SharedPreferences.clear() + 状态重置

### 🟢 未使用的模型

**位置**: `models/command.dart`
**问题**: `CmdRequest` / `CmdResponse` 已定义但从未被引用
**修复**: 在 `BleService.sendCommand()` 中使用 `CmdRequest` 构建命令

### 🟢 图表缺少时间轴

**位置**: `widgets/weight_chart.dart`
**问题**: X 轴无时间标签
**修复**: 添加 `BottomTitles` 显示时间刻度

### 🟢 调试日志清理

**位置**: 多处 (`BleService`, `liveWeightProvider`, `DeviceDetailScreen`)
**问题**: 生产代码中遗留 `print()` / `debugPrint()` 语句
**修复**: 替换为条件编译或移除
