import 'dart:async';
import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

import '../config.dart';

class WsService {
  WebSocketChannel? _channel;
  String? _token;
  Timer? _reconnectTimer;

  final _weightController = StreamController<Map<String, dynamic>>.broadcast();
  final _statusController = StreamController<Map<String, dynamic>>.broadcast();

  Stream<Map<String, dynamic>> get weightStream => _weightController.stream;
  Stream<Map<String, dynamic>> get statusStream => _statusController.stream;

  void setToken(String? token) => _token = token;

  void connect(String deviceId) {
    _disconnect();
    if (_token == null) return;

    final uri = Uri.parse('${AppConfig.wsBaseUrl}/ws/$deviceId?token=$_token');
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
      onError: (_) => _scheduleReconnect(deviceId),
      onDone: () => _scheduleReconnect(deviceId),
    );
  }

  void _scheduleReconnect(String deviceId) {
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(AppConfig.wsReconnectDelay, () => connect(deviceId));
  }

  void _disconnect() {
    _reconnectTimer?.cancel();
    _channel?.sink.close();
    _channel = null;
  }

  void dispose() {
    _disconnect();
    _weightController.close();
    _statusController.close();
  }
}

final wsServiceProvider = Provider<WsService>((ref) => WsService());
