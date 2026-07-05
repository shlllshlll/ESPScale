# ESPScale ESP-IDF 迁移计划

## 1. 项目概述

### 1.1 迁移目标
将现有 Arduino 框架固件迁移到 ESP-IDF 框架，使用 FreeRTOS 编程接口实现更稳定的时序控制。

### 1.2 核心优势
- **更稳定的时序控制**: FreeRTOS 任务调度，精确的 `vTaskDelayUntil()` 周期控制
- **更低的资源消耗**: 去除 Arduino 抽象层，直接使用 ESP-IDF 原生 API
- **更好的调试能力**: JTAG 调试、Core Dump、栈溢出检测
- **更灵活的电源管理**: 深度睡眠、Light Sleep 等模式

### 1.3 硬件平台
- **芯片**: ESP32-C3 (RISC-V, 单核, 400KB SRAM, 4MB Flash)
- **开发板**: ESP32-C3-DevKitM-1
- **外设**: HX711 称重传感器、LED (GPIO 8)

---

## 2. 技术栈对比

| 组件 | Arduino (当前) | ESP-IDF (目标) | 迁移难度 |
|------|---------------|---------------|---------|
| 编程模型 | `setup()` + `loop()` | `app_main()` + FreeRTOS 任务 | 中 |
| BLE | NimBLE-Arduino (h2zero) | ESP-IDF 原生 NimBLE | 高 |
| 称重传感器 | bogde/HX711 库 | 自定义 GPIO bit-bang 驱动 | 中 |
| MQTT | PubSubClient (knolleary) | esp_mqtt_client | 低 |
| JSON | ArduinoJson | cJSON (ESP-IDF 内置) | 低 |
| NVS | Preferences | nvs_flash | 低 |
| WiFi | Arduino WiFi | esp_wifi | 中 |
| HTTP | WiFiClient (手动 HTTP) | esp_http_client | 低 |
| 日志 | Serial.printf | ESP_LOGI/W/E | 低 |
| 构建系统 | PlatformIO Arduino | PlatformIO ESP-IDF (CMake) | 低 |

---

## 3. 项目结构设计

```
firmware-espidf/
├── CMakeLists.txt                    # 顶层 CMake 配置
├── platformio.ini                    # PlatformIO 配置
├── sdkconfig.defaults                # ESP-IDF 默认配置
├── partitions.csv                    # 分区表 (可选)
│
├── main/                             # 主组件
│   ├── CMakeLists.txt
│   ├── main.c                        # app_main() 入口
│   ├── config.h                      # 引脚/UUID/常量定义
│   └── app_event_defs.h              # 自定义事件定义
│
├── components/                       # 自定义组件
│   ├── scale_sensor/                 # HX711 称重传感器
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── hx711.h
│   │   │   └── scale_sensor.h
│   │   ├── hx711.c                   # HX711 底层 GPIO 驱动
│   │   └── scale_sensor.c            # 称重业务逻辑
│   │
│   ├── ble_service/                  # BLE GATT 服务
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   └── ble_service.h
│   │   ├── ble_gatt.c                # GATT 服务定义 + 回调
│   │   ├── ble_protocol.c            # BLE 命令解析
│   │   └── ble_weight_notify.c       # 重量数据推送
│   │
│   ├── wifi_manager/                 # WiFi 连接管理
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   └── wifi_manager.h
│   │   └── wifi_manager.c            # 事件驱动 WiFi STA
│   │
│   ├── mqtt_handler/                 # MQTT 客户端
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   └── mqtt_handler.h
│   │   └── mqtt_handler.c            # esp_mqtt_client 封装
│   │
│   ├── http_client/                  # HTTP 客户端
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   └── http_client.h
│   │   └── http_client.c             # esp_http_client 封装
│   │
│   ├── storage/                      # NVS 持久化存储
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   └── storage.h
│   │   └── storage.c                 # nvs_flash 封装
│   │
│   └── protocol/                     # 命令协议解析
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── protocol.h
│       └── protocol.c                # cJSON 命令解析
│
└── test/                             # 测试
    ├── CMakeLists.txt
    └── test_scale_sensor.c           # Unity 测试框架
```

---

## 4. FreeRTOS 任务架构

### 4.1 任务设计

| 任务名 | 优先级 | 栈大小 | 周期 | 职责 |
|--------|-------|--------|------|------|
| `scale_task` | 5 (最高) | 3072 | 400ms | HX711 读取、去皮、校准、重量数据入队 |
| `ble_task` | 4 | 8192 | 事件驱动 | BLE GATT 服务、命令解析、重量 notify |
| `network_task` | 3 | 4096 | 事件驱动 | WiFi 事件、HTTP POST、MQTT 发布 |
| `state_task` | 2 | 2048 | 100ms | 状态机转换、LED 指示灯控制 |

### 4.2 任务优先级说明
- **scale_task 最高优先级**: 确保称重数据不会因网络阻塞而丢失
- **ble_task 高于 network_task**: BLE 配网是关键功能，需要快速响应
- **state_task 最低优先级**: LED 指示灯不影响核心功能

### 4.3 任务间通信

```
┌─────────────────────────────────────────────────────────────────┐
│                         app_main()                               │
│  创建通信原语 → 初始化子系统 → 创建任务                           │
└─────────────────────────────────────────────────────────────────┘
       │
       ├── weight_queue (QueueHandle_t, 8 x weight_data_t)
       │   └── scale_task -> network_task / ble_task
       │
       ├── cmd_queue (QueueHandle_t, 4 x cmd_request_t)
       │   └── ble_task / mqtt -> scale_task / storage
       │
       ├── sys_events (EventGroupHandle_t)
       │   ├── WIFI_CONNECTED_BIT   (BIT0)
       │   ├── MQTT_CONNECTED_BIT   (BIT1)
       │   ├── BLE_CONNECTED_BIT    (BIT2)
       │   └── SCALE_READY_BIT      (BIT3)
       │
       └── config_mutex (SemaphoreHandle_t)
           └── 保护共享配置读写
```

---

## 5. 组件详细设计

### 5.1 storage (NVS 持久化)

**职责**: 管理所有配置的持久化存储

**API 设计**:
```c
esp_err_t storage_init(void);
esp_err_t storage_load_config(stored_config_t *cfg);
esp_err_t storage_save_wifi(const char *ssid, const char *pass);
esp_err_t storage_save_cal_factor(float factor);
esp_err_t storage_save_unit(const char *unit);
esp_err_t storage_save_upload_interval(uint32_t interval_ms);
esp_err_t storage_save_mode(uint8_t mode);
esp_err_t storage_save_server_url(const char *url);
esp_err_t storage_save_mqtt_config(const mqtt_config_t *cfg);
esp_err_t storage_save_api_key(const char *key);
esp_err_t storage_factory_reset(void);
```

**数据结构**:
```c
typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char mqtt_host[64];
    uint16_t mqtt_port;
    char mqtt_user[32];
    char mqtt_pass[64];
    char server_url[128];
    char device_id[24];
    char device_name[32];
    float cal_factor;
    char unit[4];
    uint32_t upload_interval_ms;
    uint8_t mode;
    char api_key[64];
    uint8_t cfg_version;
} stored_config_t;
```

### 5.2 scale_sensor (HX711 称重)

**职责**: HX711 GPIO 驱动 + 称重业务逻辑

**HX711 驱动 API**:
```c
esp_err_t hx711_init(hx711_t *dev);
bool hx711_is_ready(hx711_t *dev);
int32_t hx711_read(hx711_t *dev);
int32_t hx711_read_average(hx711_t *dev, uint8_t times);
float hx711_get_units(hx711_t *dev, uint8_t times);
void hx711_tare(hx711_t *dev, uint8_t times);
void hx711_set_scale(hx711_t *dev, float scale);
void hx711_power_down(hx711_t *dev);
void hx711_power_up(hx711_t *dev);
```

**业务层 API**:
```c
esp_err_t scale_sensor_init(void);
float scale_sensor_read_weight(uint8_t samples);
void scale_sensor_tare(uint8_t samples);
float scale_sensor_calibrate(float expected_weight);
bool scale_sensor_is_ready(void);
```

**HX711 时序要求**:
- SCK 高电平: 最小 0.2us, 最大 50us
- SCK 低电平: 最小 0.2us
- DOUT 下降沿到 SCK 上升沿: 最小 0.1us
- 使用 `ets_delay_us()` 实现精确微秒级延时

### 5.3 wifi_manager (WiFi 连接)

**职责**: WiFi STA 连接管理，事件驱动

**API 设计**:
```c
esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_connect(const char *ssid, const char *pass);
esp_err_t wifi_manager_disconnect(void);
bool wifi_manager_is_connected(void);
int8_t wifi_manager_get_rssi(void);
```

**事件处理**:
- `WIFI_EVENT_STA_START`: 自动连接
- `WIFI_EVENT_STA_DISCONNECTED`: 指数退避重连 (5s x retry, 最多 3 次)
- `IP_EVENT_STA_GOT_IP`: 设置 `WIFI_CONNECTED_BIT`

### 5.4 ble_service (BLE GATT)

**职责**: NimBLE GATT 服务、命令解析、重量推送

**GATT 特征值** (与 Arduino 版保持一致):

| 特征值 | UUID 后缀 | 属性 | 用途 |
|--------|----------|------|------|
| DeviceInfo | 2A29F239 | READ | 设备信息 JSON |
| WifiCreds | C1B2A3D4 | WRITE | WiFi 凭证 + 配置 |
| NetworkStatus | D5E6F7A8 | READ + NOTIFY | 网络状态 |
| ScaleSettings | B9C8D7E6 | READ + WRITE | 秤设置 |
| WeightStream | F5E4D3C2 | NOTIFY | 实时重量流 |
| Command | A1B2C3D4 | WRITE | JSON 命令 |
| Event | E8F7A6B5 | NOTIFY | 命令响应 |

**API 设计**:
```c
esp_err_t ble_service_init(void);
esp_err_t ble_notify_weight(const weight_data_t *data);
esp_err_t ble_notify_network_status(void);
bool ble_is_connected(void);
```

### 5.5 mqtt_handler (MQTT 客户端)

**职责**: MQTT 连接、订阅、发布

**API 设计**:
```c
esp_err_t mqtt_handler_init(void);
esp_err_t mqtt_handler_publish_weight(const weight_data_t *data);
esp_err_t mqtt_handler_publish_status(const char *status);
bool mqtt_handler_is_connected(void);
```

**MQTT Topic**:
```
espscale/{device_id}/weight     # 称重数据 (QoS 1, Retain=false)
espscale/{device_id}/status     # 设备状态 + LWT (QoS 1, Retain=true)
espscale/{device_id}/cmd        # 远程命令 (QoS 1, Retain=false)
```

**关键特性**:
- LWT (Last Will and Testament): 离线状态自动发布
- 自动订阅 cmd topic
- 事件驱动连接/断线处理

### 5.6 http_client (HTTP 客户端)

**职责**: HTTP POST 数据上报

**API 设计**:
```c
esp_err_t http_client_post_weight(const weight_data_t *data);
```

**请求格式**:
```
POST /api/v1/data HTTP/1.1
Content-Type: application/json
X-Device-ID: esp32c3-XXXXXX
X-API-Key: <api_key>

{"weight":123.4,"unit":"g","stable":true,"timestamp":12345}
```

### 5.7 protocol (命令协议)

**职责**: JSON 命令解析/构建

**API 设计**:
```c
cmd_request_t protocol_parse(const char *json);
char *protocol_build_ack(const char *cmd_name, bool success, const char *request_id);
char *protocol_build_weight(const weight_data_t *data);
```

**支持的命令**:
- `tare`: 去皮
- `calibrate`: 校准 (参数: expected_weight)
- `set_mode`: 切换模式 (0=HTTP, 1=MQTT, 2=BLE_ONLY)
- `set_config`: 修改配置
- `set_wifi`: 更新 WiFi 并重连
- `set_mqtt`: 更新 MQTT 配置
- `get_status`: 获取完整状态
- `reboot`: 重启设备
- `factory_reset`: 恢复出厂

---

## 6. 状态机设计

### 6.1 状态定义

```c
typedef enum {
    STATE_PROVISIONING,      // 等待 BLE 配网
    STATE_CONNECTING_WIFI,   // WiFi 连接中
    STATE_CONNECTING_MQTT,   // MQTT 连接中
    STATE_RUNNING,           // 正常运行
    STATE_ERROR_WIFI,        // WiFi 错误
    STATE_ERROR_MQTT,        // MQTT 错误
    STATE_ERROR_HX711,       // 传感器错误
    STATE_FACTORY_RESET,     // 恢复出厂
} device_state_t;
```

### 6.2 状态转换

```
PROVISIONING -> (WiFi 凭证写入) -> CONNECTING_WIFI
CONNECTING_WIFI -> (WiFi 连接成功) -> CONNECTING_MQTT (mode=MQTT) 或 RUNNING
CONNECTING_WIFI -> (超时) -> ERROR_WIFI
CONNECTING_MQTT -> (MQTT 连接成功) -> RUNNING
CONNECTING_MQTT -> (超时) -> ERROR_MQTT
RUNNING -> (WiFi 断线) -> ERROR_WIFI
RUNNING -> (HX711 连续 10 次失败) -> ERROR_HX711
ERROR_WIFI -> (WiFi 重连) -> CONNECTING_MQTT 或 RUNNING
ERROR_MQTT -> (MQTT 重连) -> RUNNING
ERROR_HX711 -> (传感器恢复) -> RUNNING
```

### 6.3 LED 指示

| 状态 | LED 模式 | 说明 |
|------|---------|------|
| PROVISIONING | 慢闪 (2.2s 周期) | 等待配网 |
| CONNECTING_WIFI | 快闪 (600ms 周期) | 连接中 |
| CONNECTING_MQTT | 快闪 (600ms 周期) | 连接中 |
| RUNNING | 常亮 | 正常运行 |
| ERROR_WIFI | 三短闪 | WiFi 错误 |
| ERROR_MQTT | 灭灯 | MQTT 错误 |
| ERROR_HX711 | 三中闪 | 传感器错误 |

---

## 7. 数据流设计

```
┌─────────────┐
│   HX711     │
│  传感器     │
└──────┬──────┘
       │ GPIO 读取 (400ms)
       v
┌──────────────┐    weight_queue    ┌──────────────┐
│  scale_task  │ ------------------>| network_task │
│  (优先级 5)  │                    │  (优先级 3)  │
└──────────────┘                    └──────┬───────┘
       │                                   │
       │                                   ├── HTTP POST
       │                                   └── MQTT Publish
       v
┌──────────────┐
│   ble_task   │ <--- cmd_queue ---- BLE Write
│  (优先级 4)  │
└──────────────┘
       │
       └── BLE Notify (重量数据)
```

---

## 8. sdkconfig 配置

```ini
# 目标芯片
CONFIG_IDF_TARGET="esp32c3"

# Flash
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y

# WiFi
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=12

# BLE (NimBLE)
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_ENABLED=y

# MQTT
CONFIG_MQTT_PROTOCOL_311=y

# FreeRTOS
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_UNICORE=y

# 日志
CONFIG_LOG_DEFAULT_LEVEL_INFO=y

# 栈溢出检测
CONFIG_FREERTOS_CHECK_STACKOVERFLOW=y
CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y
```

---

## 9. 迁移阶段

### Phase 1: 基础设施 (1-2天)
- [ ] 创建 ESP-IDF 项目结构
- [ ] 实现 storage 组件 (NVS)
- [ ] 实现 protocol 组件 (cJSON)
- [ ] 定义 config.h 常量

### Phase 2: 传感器 (1天)
- [ ] 实现 HX711 GPIO 驱动
- [ ] 实现 scale_sensor 业务层
- [ ] 验证称重精度

### Phase 3: 通信 (2-3天)
- [ ] 实现 wifi_manager (事件驱动)
- [ ] 实现 ble_service (NimBLE GATT)
- [ ] 实现 mqtt_handler (esp_mqtt_client)
- [ ] 实现 http_client (esp_http_client)

### Phase 4: 集成 (1-2天)
- [ ] 实现状态机
- [ ] 实现 main.c (任务创建 + 调度)
- [ ] 任务间通信集成

### Phase 5: 测试与优化 (1-2天)
- [ ] 内存监控 (确保 >50KB 空闲堆)
- [ ] 栈溢出检测
- [ ] 长时间运行测试
- [ ] 功耗优化 (可选)

---

## 10. 关键注意事项

### 10.1 内存管理
- ESP32-C3 仅 400KB SRAM，BLE + WiFi 同时运行时内存紧张
- 使用 NimBLE 而非 Bluedroid (节省 ~60% 内存)
- cJSON 使用后立即 `cJSON_Delete()`
- 避免动态分配，优先栈分配

### 10.2 BLE 与 WiFi 共存
- ESP32-C3 的 BLE 和 WiFi 共享射频
- 避免同时高频操作
- BLE 连接间隔不要太短 (建议 20-30ms)
- WiFi 扫描时 BLE 可能短暂中断

### 10.3 任务优先级
- 传感器任务优先级高于网络任务
- 网络阻塞不能影响称重数据采集
- 使用 `vTaskDelayUntil()` 实现精确周期

### 10.4 看门狗
- ESP-IDF 默认启用 Task WDT
- 每个任务必须定期调用 `vTaskDelay()` 或 `taskYIELD()`
- 长时间操作需要分段执行

### 10.5 资源约束
- 目标空闲堆: >50KB
- JSON payload 限制: 512 字节
- BLE MTU: 默认 256 字节
- 任务栈大小: 根据实际使用调整

---

## 11. 开发命令

```bash
# 编译
cd firmware-espidf && ~/.platformio/penv/bin/pio run

# 编译并烧录
cd firmware-espidf && ~/.platformio/penv/bin/pio run -t upload

# 串口监视 (115200)
cd firmware-espidf && ~/.platformio/penv/bin/pio device monitor

# 清理构建
cd firmware-espidf && ~/.platformio/penv/bin/pio run -t clean
```

---

## 12. 参考资源

- [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32c3/)
- [ESP-IDF API 参考](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32c3/api-reference/)
- [FreeRTOS 任务管理](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32c3/api-reference/system/freertos.html)
- [NimBLE 主机栈](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32c3/api-reference/bluetooth/nimble/index.html)
- [ESP-MQTT](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32c3/api-reference/protocols/mqtt.html)
