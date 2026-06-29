import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../services/api_service.dart';
import '../providers/app_providers.dart';

class DeviceSettingsScreen extends ConsumerStatefulWidget {
  final String deviceId;
  const DeviceSettingsScreen({super.key, required this.deviceId});

  @override
  ConsumerState<DeviceSettingsScreen> createState() => _DeviceSettingsScreenState();
}

class _DeviceSettingsScreenState extends ConsumerState<DeviceSettingsScreen> {
  final _nameCtrl = TextEditingController();
  final _unitCtrl = TextEditingController();
  final _serverUrlCtrl = TextEditingController();
  final _mqttHostCtrl = TextEditingController();
  final _mqttPortCtrl = TextEditingController(text: '1883');
  int _uploadInterval = 5000;
  // Mode uses integers to match firmware: 0=HTTP, 1=MQTT, 2=BLE
  int _mode = 0;
  bool _loading = true;

  /// Map API string mode to internal integer.
  int _modeStringToInt(String s) {
    switch (s) {
      case 'mqtt': return 1;
      case 'ble_only':
      case 'local': return 2;
      default: return 0; // http_direct / remote
    }
  }

  String _modeIntToString(int i) {
    switch (i) {
      case 1: return 'mqtt';
      case 2: return 'ble_only';
      default: return 'http_direct';
    }
  }

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final api = ref.read(apiServiceProvider);
    try {
      final device = await api.getDevice(widget.deviceId);
      if (mounted) {
        _nameCtrl.text = device.name;
        _unitCtrl.text = device.unit;
        _uploadInterval = device.uploadIntervalMs;
        _mode = _modeStringToInt(device.mode);
        _serverUrlCtrl.text = device.serverUrl ?? '';
        // mqtt_broker is stored as "host:port" or just "host" in DB
        final broker = device.mqttBroker ?? '';
        if (broker.isNotEmpty) {
          final parts = broker.split(':');
          _mqttHostCtrl.text = parts[0];
          if (parts.length > 1) _mqttPortCtrl.text = parts[1];
        }
        setState(() => _loading = false);
      }
    } catch (e) {
      if (mounted) setState(() => _loading = false);
    }
  }

  Future<void> _save() async {
    final api = ref.read(apiServiceProvider);
    final modeStr = _modeIntToString(_mode);
    final mqttBroker = _mode == 1 && _mqttHostCtrl.text.trim().isNotEmpty
        ? '${_mqttHostCtrl.text.trim()}:${_mqttPortCtrl.text.trim()}'
        : '';
    await api.updateDevice(widget.deviceId, {
      'name': _nameCtrl.text.trim(),
      'unit': _unitCtrl.text.trim(),
      'upload_interval_ms': _uploadInterval,
      'mode': modeStr,
      'server_url': _mode == 0 ? _serverUrlCtrl.text.trim() : '',
      'mqtt_broker': mqttBroker,
    });
    await ref.read(deviceListProvider.notifier).refresh();
    if (mounted) Navigator.pop(context);
  }

  @override
  void dispose() {
    _nameCtrl.dispose();
    _unitCtrl.dispose();
    _serverUrlCtrl.dispose();
    _mqttHostCtrl.dispose();
    _mqttPortCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (_loading) return const Scaffold(body: Center(child: CircularProgressIndicator()));

    return Scaffold(
      appBar: AppBar(title: const Text('Device Settings')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          TextField(
            controller: _nameCtrl,
            decoration: const InputDecoration(labelText: 'Device Name'),
          ),
          const SizedBox(height: 16),
          DropdownButtonFormField<int>(
            initialValue: _uploadInterval,
            decoration: const InputDecoration(labelText: 'Upload Interval'),
            items: const [
              DropdownMenuItem(value: 1000, child: Text('1 second')),
              DropdownMenuItem(value: 5000, child: Text('5 seconds')),
              DropdownMenuItem(value: 10000, child: Text('10 seconds')),
              DropdownMenuItem(value: 30000, child: Text('30 seconds')),
              DropdownMenuItem(value: 60000, child: Text('60 seconds')),
            ],
            onChanged: (v) {
              if (v != null) setState(() => _uploadInterval = v);
            },
          ),
          const SizedBox(height: 16),
          TextField(
            controller: _unitCtrl,
            decoration: const InputDecoration(labelText: 'Weight Unit (g / kg / lb)'),
          ),
          const SizedBox(height: 24),
          const Text('Report Mode', style: TextStyle(fontSize: 14, fontWeight: FontWeight.w500)),
          const SizedBox(height: 8),
          SegmentedButton<int>(
            segments: const [
              ButtonSegment(value: 0, label: Text('HTTP'), icon: Icon(Icons.http, size: 18)),
              ButtonSegment(value: 1, label: Text('MQTT'), icon: Icon(Icons.router, size: 18)),
              ButtonSegment(value: 2, label: Text('BLE'), icon: Icon(Icons.bluetooth, size: 18)),
            ],
            selected: {_mode},
            onSelectionChanged: (sel) => setState(() => _mode = sel.first),
          ),
          const SizedBox(height: 16),
          if (_mode == 0) ...[
            TextField(
              controller: _serverUrlCtrl,
              decoration: const InputDecoration(
                labelText: 'HTTP Server URL',
                prefixIcon: Icon(Icons.dns),
                hintText: 'http://192.168.1.100:8000',
              ),
              keyboardType: TextInputType.url,
            ),
          ],
          if (_mode == 1) ...[
            Row(
              children: [
                Expanded(
                  flex: 3,
                  child: TextField(
                    controller: _mqttHostCtrl,
                    decoration: const InputDecoration(
                      labelText: 'MQTT Broker',
                      prefixIcon: Icon(Icons.dns),
                      hintText: '192.168.1.100',
                    ),
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  flex: 1,
                  child: TextField(
                    controller: _mqttPortCtrl,
                    decoration: const InputDecoration(labelText: 'Port'),
                    keyboardType: TextInputType.number,
                  ),
                ),
              ],
            ),
          ],
          if (_mode == 2)
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Colors.blue.shade50,
                borderRadius: BorderRadius.circular(8),
              ),
              child: const Text('BLE-only: weight streams directly to this app over Bluetooth. No server or WiFi needed.'),
            ),
          const SizedBox(height: 32),
          ElevatedButton.icon(
            onPressed: _save,
            icon: const Icon(Icons.save),
            label: const Text('Save Settings'),
            style: ElevatedButton.styleFrom(minimumSize: const Size.fromHeight(48)),
          ),
        ],
      ),
    );
  }
}
