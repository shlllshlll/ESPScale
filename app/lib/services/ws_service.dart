import 'dart:async';
import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

import '../config.dart';

class WsService {
  WebSocketChannel? _channel;
  Timer? _reconnectTimer;
  int _reconnectAttempts = 0;
  static const int _maxReconnectAttempts = 5;
  String? _currentDeviceId;

  final _weightController = StreamController<Map<String, dynamic>>.broadcast();
  final _statusController = StreamController<Map<String, dynamic>>.broadcast();

  Stream<Map<String, dynamic>> get weightStream => _weightController.stream;
  Stream<Map<String, dynamic>> get statusStream => _statusController.stream;

  void connect({required String deviceId, required String wsBaseUrl, String apiKey = ''}) {
    if (_currentDeviceId == deviceId) return;
    _disconnect();

    _currentDeviceId = deviceId;

    try {
      var wsUrl = '$wsBaseUrl/ws/$deviceId';
      if (apiKey.isNotEmpty) {
        wsUrl += '?api_key=$apiKey';
      }
      final uri = Uri.parse(wsUrl);
      _channel = WebSocketChannel.connect(uri);
      _channel!.stream.listen(
        (data) {
          try {
            final msg = jsonDecode(data as String) as Map<String, dynamic>;
            final type = msg['type'] as String?;
            if (type == 'weight_update') {
              _weightController.add(msg['data'] as Map<String, dynamic>);
            } else if (type == 'device_status') {
              _statusController.add(msg['data'] as Map<String, dynamic>);
            }
          } catch (_) {}
        },
        onError: (_) => _scheduleReconnect(deviceId, wsBaseUrl),
        onDone: () => _scheduleReconnect(deviceId, wsBaseUrl),
      );
      _reconnectAttempts = 0;
    } catch (_) {
      // Server not running — silently skip
    }
  }

  void _scheduleReconnect(String deviceId, String wsBaseUrl) {
    if (_reconnectAttempts >= _maxReconnectAttempts) return;
    _reconnectAttempts++;
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(AppConfig.wsReconnectDelay, () => connect(deviceId: deviceId, wsBaseUrl: wsBaseUrl));
  }

  void _disconnect() {
    _reconnectTimer?.cancel();
    _channel?.sink.close();
    _channel = null;
    _currentDeviceId = null;
  }

  void dispose() {
    _disconnect();
    _weightController.close();
    _statusController.close();
  }
}

final wsServiceProvider = Provider<WsService>((ref) => WsService());
