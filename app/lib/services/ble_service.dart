import 'dart:async';
import 'dart:convert';
import 'dart:math';

import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../config.dart';

enum ProvisionState { scanning, connecting, discovering, writing, waitingNetwork, done, error }

class ProvisionResult {
  final bool success;
  final String? error;
  final String? deviceId;
  const ProvisionResult({required this.success, this.error, this.deviceId});
}

class BleService {
  BluetoothDevice? _device;
  final _connStateController = StreamController<BluetoothConnectionState>.broadcast();

  Stream<BluetoothConnectionState> get connectionState => _connStateController.stream;
  BluetoothDevice? get connectedDevice => _device;

  Stream<List<ScanResult>> scanForScales({Duration timeout = const Duration(seconds: 10)}) {
    final controller = StreamController<List<ScanResult>>.broadcast();
    final results = <ScanResult>[];

    FlutterBluePlus.startScan(
      withServices: [Guid(AppConfig.serviceUuid)],
      timeout: timeout,
    );

    FlutterBluePlus.scanResults.listen((r) {
      results
        ..removeWhere((e) => e.device.remoteId.str.isEmpty)
        ..addAll(r.where((s) =>
            s.device.platformName.startsWith('ESPScale-')));
      controller.add(results.toList());
    });

    FlutterBluePlus.isScanning.listen((scanning) {
      if (!scanning) controller.close();
    });

    return controller.stream;
  }

  Future<void> connect(String deviceId) async {
    _device = BluetoothDevice.fromId(deviceId);
    await _device!.connect(autoConnect: false);
    _connStateController.add(BluetoothConnectionState.connected);
    await _device!.discoverServices();
  }

  Future<Map<String, dynamic>> readCharacteristic(String uuid) async {
    final services = _device?.servicesList ?? [];
    for (final service in services) {
      for (final char in service.characteristics) {
        if (char.uuid.str.toLowerCase() == uuid.toLowerCase()) {
          final value = await char.read();
          return jsonDecode(utf8.decode(value)) as Map<String, dynamic>;
        }
      }
    }
    throw Exception('Characteristic $uuid not found');
  }

  Future<void> writeCharacteristic(String uuid, Map<String, dynamic> data) async {
    final services = _device?.servicesList ?? [];
    for (final service in services) {
      for (final char in service.characteristics) {
        if (char.uuid.str.toLowerCase() == uuid.toLowerCase()) {
          await char.write(utf8.encode(jsonEncode(data)), withoutResponse: false);
          return;
        }
      }
    }
    throw Exception('Characteristic $uuid not found');
  }

  Stream<Map<String, dynamic>> notifyCharacteristic(String uuid) {
    final controller = StreamController<Map<String, dynamic>>.broadcast();
    final services = _device?.servicesList ?? [];
    for (final service in services) {
      for (final char in service.characteristics) {
        if (char.uuid.str.toLowerCase() == uuid.toLowerCase()) {
          char.onValueReceived.listen((value) {
            try {
              controller.add(jsonDecode(utf8.decode(value)) as Map<String, dynamic>);
            } catch (_) {}
          });
          char.setNotifyValue(true);
        }
      }
    }
    return controller.stream;
  }

  Future<ProvisionResult> provisionDevice({
    required String ssid,
    required String password,
  }) async {
    try {
      final deviceInfo = await readCharacteristic(AppConfig.charDeviceInfo);
      final deviceId = deviceInfo['device_id'] as String? ?? '';

      await writeCharacteristic(AppConfig.charWifiCreds, {
        'ssid': ssid,
        'password': password,
      });

      final statusStream = notifyCharacteristic(AppConfig.charNetworkStatus);

      final completer = Completer<ProvisionResult>();
      Timer? timeout = Timer(const Duration(seconds: 20), () {
        if (!completer.isCompleted) {
          completer.complete(const ProvisionResult(success: false, error: 'WiFi connect timeout'));
        }
      });

      statusStream.listen((status) {
        final wifi = status['wifi'] as Map<String, dynamic>?;
        if (wifi != null && wifi['connected'] == true) {
          timeout?.cancel();
          if (!completer.isCompleted) {
            completer.complete(ProvisionResult(success: true, deviceId: deviceId));
          }
        }
      });

      return completer.future;
    } catch (e) {
      return ProvisionResult(success: false, error: e.toString());
    }
  }

  Future<void> sendCommand(String cmd, Map<String, dynamic> params, String requestId) async {
    await writeCharacteristic(AppConfig.charCommand, {
      'cmd': cmd,
      'params': params,
      'request_id': requestId,
    });
  }

  Future<void> disconnect() async {
    await _device?.disconnect();
    _device = null;
    _connStateController.add(BluetoothConnectionState.disconnected);
  }

  void dispose() {
    _connStateController.close();
  }
}

final bleServiceProvider = Provider<BleService>((ref) => BleService());
