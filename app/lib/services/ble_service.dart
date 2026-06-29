import 'dart:async';
import 'dart:convert';

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
  BluetoothConnectionState _connState = BluetoothConnectionState.disconnected;
  late final StreamController<BluetoothConnectionState> _connStateController;

  BleService() {
    _connStateController = StreamController<BluetoothConnectionState>.broadcast(
      onListen: () {
        // Replay the last known state to new listeners (e.g. after navigation)
        // Use add() sync here — the stream hasn't delivered anything to this listener yet
        _connStateController.add(_connState);
      },
    );
  }

  Stream<BluetoothConnectionState> get connectionState => _connStateController.stream;
  BluetoothDevice? get connectedDevice => _device;

  Stream<List<ScanResult>> scanForScales({Duration timeout = const Duration(seconds: 10)}) {
    late final StreamController<List<ScanResult>> controller;
    StreamSubscription? scanResultsSub;
    StreamSubscription? isScanningSub;
    final results = <ScanResult>[];

    void cleanup() {
      scanResultsSub?.cancel();
      isScanningSub?.cancel();
      if (!controller.isClosed) controller.close();
      FlutterBluePlus.stopScan();
    }

    controller = StreamController<List<ScanResult>>.broadcast(
      onCancel: cleanup,
    );

    FlutterBluePlus.startScan(
      withServices: [Guid(AppConfig.serviceUuid)],
      timeout: timeout,
    ).catchError((_) {
      // Bluetooth unavailable (e.g. iOS simulator) — silently ignore
      cleanup();
    });

    scanResultsSub = FlutterBluePlus.scanResults.listen((r) {
      if (controller.isClosed) return;
      final seen = results.map((e) => e.device.remoteId.str).toSet();
      results
        ..removeWhere((e) => e.device.remoteId.str.isEmpty)
        ..addAll(r.where((s) => seen.add(s.device.remoteId.str)));
      controller.add(results.toList());
    });

    isScanningSub = FlutterBluePlus.isScanning.skip(1).where((s) => !s).listen((_) {
      cleanup();
    });

    return controller.stream;
  }

  Future<void> connect(String deviceId) async {
    _device = BluetoothDevice.fromId(deviceId);
    await _device!.connect(autoConnect: false, license: License.nonprofit);
    _connState = BluetoothConnectionState.connected;
    _connStateController.add(_connState);
    await _device!.discoverServices();

    // Listen for unexpected disconnections
    _device?.connectionState.listen((state) {
      if (state == BluetoothConnectionState.disconnected) {
        _device = null;
        _connState = BluetoothConnectionState.disconnected;
        _connStateController.add(_connState);
      }
    });
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
    late final StreamController<Map<String, dynamic>> controller;
    StreamSubscription<List<int>>? valueSub;

    controller = StreamController<Map<String, dynamic>>.broadcast(
      onListen: () {
        final services = _device?.servicesList ?? [];
        if (_device == null) {
          print('[BleService] notifyCharacteristic: _device is null!');
          return;
        }
        print('[BleService] notifyCharacteristic: searching for $uuid in ${services.length} services');
        bool found = false;
        for (final service in services) {
          for (final char in service.characteristics) {
            final charUuid = char.uuid.str.toLowerCase();
            print('[BleService]   char: $charUuid props=${char.properties}');
            if (charUuid == uuid.toLowerCase()) {
              found = true;
              print('[BleService]   FOUND! Subscribing to notifications...');
              valueSub = char.onValueReceived.listen((value) {
                final decoded = utf8.decode(value);
                print('[BleService] <<< BLE notify received: $decoded');
                if (!controller.isClosed) {
                  try {
                    controller.add(jsonDecode(decoded) as Map<String, dynamic>);
                  } catch (e) {
                    print('[BleService] JSON parse error: $e');
                  }
                }
              });
              char.setNotifyValue(true).then((_) {
                print('[BleService] setNotifyValue(true) succeeded for $charUuid');
              }).catchError((e) {
                print('[BleService] setNotifyValue(true) FAILED: $e');
              });
            }
          }
        }
        if (!found) {
          print('[BleService] WARNING: characteristic $uuid NOT FOUND in services!');
        }
      },
      onCancel: () {
        print('[BleService] notifyCharacteristic: onCancel for $uuid');
        valueSub?.cancel();
        final services = _device?.servicesList ?? [];
        for (final service in services) {
          for (final char in service.characteristics) {
            if (char.uuid.str.toLowerCase() == uuid.toLowerCase()) {
              char.setNotifyValue(false).catchError((_) {});
            }
          }
        }
      },
    );

    return controller.stream;
  }

  /// Send get_status command and return the device's full config via Event notify.
  /// Returns null if the device doesn't respond within [timeout].
  Future<Map<String, dynamic>?> getDeviceStatus({Duration timeout = const Duration(seconds: 5)}) async {
    final completer = Completer<Map<String, dynamic>?>();
    StreamSubscription? sub;
    Timer? timer;

    sub = notifyCharacteristic(AppConfig.charEvent).listen((event) {
      if (!completer.isCompleted) {
        completer.complete(event);
      }
    });

    timer = Timer(timeout, () {
      if (!completer.isCompleted) {
        completer.complete(null);
      }
    });

    try {
      await sendCommand('get_status', {}, 'status-${DateTime.now().millisecondsSinceEpoch}');
    } catch (_) {
      sub.cancel();
      timer.cancel();
      return null;
    }

    final result = await completer.future;
    sub.cancel();
    timer.cancel();
    return result;
  }

  Future<ProvisionResult> provisionDevice({
    String ssid = '',
    String password = '',
    int mode = 0,
    String serverUrl = '',
    String mqttHost = '',
    int mqttPort = 1883,
  }) async {
    try {
      final deviceInfo = await readCharacteristic(AppConfig.charDeviceInfo);
      final deviceId = deviceInfo['device_id'] as String? ?? '';
      final bool needsWifi = mode != 2 && ssid.isNotEmpty;

      if (needsWifi) {
        // Send WiFi creds + extended config in one JSON write
        await writeCharacteristic(AppConfig.charWifiCreds, {
          'ssid': ssid,
          'password': password,
          'mode': mode,
          'server_url': serverUrl,
          'mqtt_host': mqttHost,
          'mqtt_port': mqttPort,
        });
      } else {
        // BLE-only or no WiFi: send config via command channel only
        await sendCommand('set_config', {
          'mode': mode,
          'server_url': serverUrl,
          'mqtt_host': mqttHost,
          'mqtt_port': mqttPort,
        }, 'prov-${DateTime.now().millisecondsSinceEpoch}');
      }

      // For BLE-only mode, skip WiFi wait — config was sent via set_config
      if (mode == 2) {
        return ProvisionResult(success: true, deviceId: deviceId);
      }

      final completer = Completer<ProvisionResult>();

      // Subscribe to notifications
      StreamSubscription? notifySub;
      notifySub = notifyCharacteristic(AppConfig.charNetworkStatus).listen((status) {
        final wifi = status['wifi'] as Map<String, dynamic>?;
        if (wifi != null && wifi['connected'] == true) {
          if (!completer.isCompleted) {
            completer.complete(ProvisionResult(success: true, deviceId: deviceId));
          }
        }
      });

      // Poll every 2s as fallback in case notification is missed
      Timer? pollTimer;
      pollTimer = Timer.periodic(const Duration(seconds: 2), (t) {
        if (completer.isCompleted) {
          t.cancel();
          return;
        }
        readCharacteristic(AppConfig.charNetworkStatus).then((status) {
          final wifi = status['wifi'] as Map<String, dynamic>?;
          if (wifi != null && wifi['connected'] == true) {
            if (!completer.isCompleted) {
              completer.complete(ProvisionResult(success: true, deviceId: deviceId));
            }
          }
        }).catchError((_) {}); // Ignore read errors during polling
      });

      Timer? timeout = Timer(const Duration(seconds: 30), () {
        if (!completer.isCompleted) {
          completer.complete(const ProvisionResult(success: false, error: 'WiFi connect timeout'));
        }
      });

      // Cleanup on completion
      final result = await completer.future;
      notifySub.cancel();
      pollTimer.cancel();
      timeout.cancel();

      // After WiFi connects, also send set_config via command channel as backup
      if (result.success) {
        try {
          await sendCommand('set_config', {
            'mode': mode,
            'server_url': serverUrl,
            'mqtt_host': mqttHost,
            'mqtt_port': mqttPort,
          }, 'prov-${DateTime.now().millisecondsSinceEpoch}');
        } catch (_) {
          // Command channel might not be available; WiFi creds write already sent the config
        }
      }

      return result;
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
    _connState = BluetoothConnectionState.disconnected;
    _connStateController.add(_connState);
  }

  void dispose() {
    _connStateController.close();
  }
}

final bleServiceProvider = Provider<BleService>((ref) => BleService());
