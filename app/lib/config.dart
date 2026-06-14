class AppConfig {
  static const String appName = 'ESPScale';
  static const String version = '0.2.0';

  // Server
  static const String apiBaseUrl = 'http://localhost:8000';
  static const String wsBaseUrl = 'ws://localhost:8000';

  // BLE UUIDs — suffix group: DD6C-4ED8-9555-3BE20F962A74
  static const String serviceUuid = 'F3860834-DD6C-4ED8-9555-3BE20F962A74';
  static const String charDeviceInfo = '2A29F239-DD6C-4ED8-9555-3BE20F962A74';
  static const String charWifiCreds = 'C1B2A3D4-DD6C-4ED8-9555-3BE20F962A74';
  static const String charNetworkStatus = 'D5E6F7A8-DD6C-4ED8-9555-3BE20F962A74';
  static const String charScaleSettings = 'B9C8D7E6-DD6C-4ED8-9555-3BE20F962A74';
  static const String charWeightStream = 'F5E4D3C2-DD6C-4ED8-9555-3BE20F962A74';
  static const String charCommand = 'A1B2C3D4-DD6C-4ED8-9555-3BE20F962A74';
  static const String charEvent = 'E8F7A6B5-DD6C-4ED8-9555-3BE20F962A74';

  static const Duration bleScanTimeout = Duration(seconds: 10);
  static const Duration wsReconnectDelay = Duration(seconds: 3);
}
