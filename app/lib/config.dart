import 'package:flutter/foundation.dart';

/// Returns the default API base URL based on build mode.
/// Debug → localhost, Release → production.
String getDefaultApiBaseUrl() {
  return kReleaseMode ? 'https://espscale.shlll.top' : 'http://localhost:8000';
}

/// Converts an HTTP(S) base URL to a WebSocket URL.
/// E.g. http://localhost:8000 → ws://localhost:8000
///      https://espscale.shlll.top → wss://espscale.shlll.top
String httpToWsUrl(String httpUrl) {
  final uri = Uri.parse(httpUrl);
  final wsScheme = uri.scheme == 'https' ? 'wss' : 'ws';
  return '$wsScheme://${uri.host}${uri.hasPort ? ':${uri.port}' : ''}';
}

class AppConfig {
  static const String appName = 'ESPScale';
  static const String version = '0.2.0';

  // No longer hard-coded — use ServerConfig provider instead.
  // These are kept as fallback for places that can't access providers.
  static String get apiBaseUrl => getDefaultApiBaseUrl();
  static String get wsBaseUrl => httpToWsUrl(getDefaultApiBaseUrl());

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

/// Runtime server configuration, backed by SharedPreferences.
class ServerConfig {
  final String apiBaseUrl;
  final String wsBaseUrl;
  final String appApiKey;

  const ServerConfig({
    required this.apiBaseUrl,
    required this.wsBaseUrl,
    this.appApiKey = '',
  });

  bool get isDefault => apiBaseUrl == getDefaultApiBaseUrl();

  ServerConfig copyWith({
    String? apiBaseUrl,
    String? wsBaseUrl,
    String? appApiKey,
  }) {
    return ServerConfig(
      apiBaseUrl: apiBaseUrl ?? this.apiBaseUrl,
      wsBaseUrl: wsBaseUrl ?? this.wsBaseUrl,
      appApiKey: appApiKey ?? this.appApiKey,
    );
  }
}
