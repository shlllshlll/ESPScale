import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../config.dart';
import '../models/device.dart';
import '../models/weight_record.dart';
import '../providers/app_providers.dart';
import '../services/api_service.dart';
import '../services/ble_service.dart';
import '../services/saved_measurement_store.dart';
import '../services/ws_service.dart';
import '../widgets/connection_indicator.dart';
import '../widgets/weight_chart.dart';
import '../widgets/weight_display.dart';

class DeviceDetailScreen extends ConsumerStatefulWidget {
  final String deviceId;
  const DeviceDetailScreen({super.key, required this.deviceId});

  @override
  ConsumerState<DeviceDetailScreen> createState() => _DeviceDetailScreenState();
}

class _DeviceDetailScreenState extends ConsumerState<DeviceDetailScreen> {
  DeviceModel? _device;
  List<WeightRecord> _records = [];
  bool _loading = true;
  bool _serverAvailable = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    // Try server first (gives us historical records), fall back to BLE
    print('[Detail] _load: trying server for ${widget.deviceId}');
    try {
      final api = ref.read(apiServiceProvider);
      final device = await api.getDevice(widget.deviceId);
      final records = await api.fetchRecords(widget.deviceId, limit: 200);
      if (mounted) {
        setState(() {
          _device = device;
          _records = records;
          _serverAvailable = true;
          _loading = false;
        });
      }
      print('[Detail] _load: server OK, mode=${device.mode}');
      _startLiveWeight();
      return;
    } catch (e) {
      print('[Detail] _load: server unavailable ($e), falling back to BLE');
      // Server unavailable — fall through to BLE
    }

    // Server unavailable — read device info from BLE
    try {
      final ble = ref.read(bleServiceProvider);
      final info = await ble.readCharacteristic(AppConfig.charDeviceInfo);
      print('[Detail] _load: BLE device info raw=$info');
      if (mounted) {
        final rawMode = info['mode'];
        final modeStr = rawMode is int
            ? (rawMode == 0 ? 'http_direct' : (rawMode == 1 ? 'mqtt' : 'ble_only'))
            : (rawMode?.toString() ?? 'ble_only');
        print('[Detail] _load: BLE mode raw=$rawMode mapped=$modeStr');
        setState(() {
          _device = DeviceModel(
            deviceId: (info['device_id'] as String?) ?? widget.deviceId,
            name: (info['name'] as String?) ?? widget.deviceId,
            firmwareVer: (info['firmware_version'] as String?) ?? '',
            mode: modeStr,
            createdAt: DateTime.now(),
            updatedAt: DateTime.now(),
          );
          _records = [];
          _serverAvailable = false;
          _loading = false;
        });
      }
      print('[Detail] _load: BLE OK, calling _startLiveWeight (force BLE since server unavailable)');
      _startLiveWeight(forceBle: true);
    } catch (e) {
      print('[Detail] _load: BLE fallback failed: $e');
      if (mounted) {
        setState(() {
          _error = 'Cannot reach device (BLE + server both unavailable)';
          _loading = false;
        });
      }
    }
  }

  void _startLiveWeight({bool forceBle = false}) {
    final isBleConnected = ref.read(bleConnectionProvider).value == BluetoothConnectionState.connected;
    final mode = _device?.mode ?? 'ble_only';
    print('[Detail] _startLiveWeight: deviceId=${widget.deviceId} mode=$mode ble=$isBleConnected forceBle=$forceBle');

    if (isBleConnected || forceBle) {
      // Preferred path: real-time weight from BLE notify (lowest latency, works offline)
      print('[Detail] _startLiveWeight: using BLE notify');
      ref.read(weightSourceProvider.notifier).set(WeightSource.ble);
    } else {
      // Fallback: BLE unavailable, use WebSocket stream from server
      print('[Detail] _startLiveWeight: BLE not connected, falling back to WS');
      final config = ref.read(serverConfigProvider);
      final wsBase = config.value?.wsBaseUrl ?? AppConfig.wsBaseUrl;
      final ws = ref.read(wsServiceProvider);
      ws.connect(deviceId: widget.deviceId, wsBaseUrl: wsBase);
      ref.read(weightSourceProvider.notifier).set(WeightSource.ws);
    }
  }

  @override
  void dispose() {
    ref.read(weightSourceProvider.notifier).set(WeightSource.none);
    super.dispose();
  }

  Future<void> _sendCommand(String cmd, {Map<String, dynamic> params = const {}}) async {
    final ble = ref.read(bleServiceProvider);
    await ble.sendCommand(cmd, params, DateTime.now().millisecondsSinceEpoch.toRadixString(16));
    // Refresh device info after command
    try {
      final info = await ble.readCharacteristic(AppConfig.charDeviceInfo);
      if (mounted) {
        final rawMode = info['mode'];
        final modeStr = rawMode is int
            ? (rawMode == 0 ? 'http_direct' : (rawMode == 1 ? 'mqtt' : 'ble_only'))
            : (rawMode?.toString() ?? 'ble_only');
        setState(() {
          _device = DeviceModel(
            deviceId: (info['device_id'] as String?) ?? widget.deviceId,
            name: (info['name'] as String?) ?? widget.deviceId,
            firmwareVer: (info['firmware_version'] as String?) ?? '',
            mode: modeStr,
            createdAt: _device?.createdAt ?? DateTime.now(),
            updatedAt: DateTime.now(),
          );
        });
      }
    } catch (_) {}
  }

  @override
  Widget build(BuildContext context) {
    // Auto-switch to BLE as soon as it connects (preferred real-time source)
    ref.listen<AsyncValue<BluetoothConnectionState>>(bleConnectionProvider, (prev, next) {
      if (next.value == BluetoothConnectionState.connected &&
          ref.read(weightSourceProvider) != WeightSource.ble) {
        ref.read(weightSourceProvider.notifier).set(WeightSource.ble);
      }
    });

    final weightReading = ref.watch(liveWeightProvider).value;
    final isBleConnected = ref.watch(bleConnectionProvider).value == BluetoothConnectionState.connected;
    final showChip = !_serverAvailable;
    final chipLabel = isBleConnected ? 'BLE Mode' : 'Offline';
    final chipColor = isBleConnected ? Colors.blue.shade100 : Colors.orange.shade100;

    return Scaffold(
      appBar: AppBar(
        title: Text(_device?.name ?? widget.deviceId),
        actions: [
          IconButton(
            icon: const Icon(Icons.history),
            tooltip: 'History',
            onPressed: () => Navigator.of(context).pushNamed('/history', arguments: widget.deviceId),
          ),
          if (showChip)
            Chip(
              label: Text(chipLabel, style: const TextStyle(fontSize: 12)),
              backgroundColor: chipColor,
              padding: EdgeInsets.zero,
              visualDensity: VisualDensity.compact,
            ),
          const SizedBox(width: 4),
          const ConnectionIndicator(),
          IconButton(
            icon: const Icon(Icons.settings),
            onPressed: () => Navigator.of(context).pushNamed('/device-settings', arguments: widget.deviceId),
          ),
        ],
      ),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : _error != null
              ? Center(
                  child: Padding(
                    padding: const EdgeInsets.all(24),
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text(_error!, textAlign: TextAlign.center),
                        const SizedBox(height: 16),
                        FilledButton.icon(
                          onPressed: () { setState(() { _loading = true; _error = null; }); _load(); },
                          icon: const Icon(Icons.refresh),
                          label: const Text('Retry'),
                        ),
                      ],
                    ),
                  ),
                )
              : RefreshIndicator(
                  onRefresh: _load,
                  child: ListView(
                    padding: const EdgeInsets.all(16),
                    children: [
                      WeightDisplay(
                        weight: weightReading?.weight ?? _device?.lastWeight ?? 0,
                        unit: _device?.unit ?? 'g',
                        stable: weightReading?.stable ?? true,
                      ),
                      const SizedBox(height: 16),
                      if (_serverAvailable && _records.isNotEmpty) ...[
                        SizedBox(
                          height: 220,
                          child: WeightChart(records: _records),
                        ),
                      ] else ...[
                        SizedBox(
                          height: 220,
                          child: Center(
                            child: Text(
                              _serverAvailable ? 'No weight records yet' : 'Historical charts require server',
                              style: TextStyle(color: Colors.grey.shade500),
                            ),
                          ),
                        ),
                      ],
                      const SizedBox(height: 16),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                        children: [
                          _ActionButton(icon: Icons.balance, label: 'Tare', onTap: () => _sendCommand('tare')),
                          _ActionButton(icon: Icons.tune, label: 'Calibrate', onTap: () => Navigator.of(context).pushNamed('/calibrate', arguments: widget.deviceId)),
                          _ActionButton(icon: Icons.bookmark_add, label: 'Record', onTap: () => _recordCurrent()),
                        ],
                      ),
                    ],
                  ),
                ),
    );
  }

  /// Save the current weight reading (from BLE/WS) as a local measurement.
  Future<void> _recordCurrent() async {
    final reading = ref.read(liveWeightProvider).value;
    final w = reading?.weight ?? _device?.lastWeight;
    if (w == null) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('No weight reading available — connect to the device first')),
      );
      return;
    }
    final unit = reading?.unit ?? _device?.unit ?? 'g';
    final saved = SavedMeasurement(
      deviceId: widget.deviceId,
      weight: w,
      unit: unit,
      recordedAt: DateTime.now(),
    );
    await SavedMeasurementStore.add(saved);
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text('Recorded ${w.toStringAsFixed(w < 10 ? 2 : 1)} $unit'),
        duration: const Duration(seconds: 2),
        action: SnackBarAction(
          label: 'View',
          onPressed: () => Navigator.of(context).pushNamed('/history', arguments: widget.deviceId),
        ),
      ),
    );
  }
}

class _ActionButton extends StatelessWidget {
  final IconData icon;
  final String label;
  final VoidCallback onTap;
  const _ActionButton({required this.icon, required this.label, required this.onTap});

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 36, color: Theme.of(context).colorScheme.primary),
          const SizedBox(height: 4),
          Text(label),
        ],
      ),
    );
  }
}
