import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../config.dart';
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
  int _uploadInterval = 5000;
  String _mode = 'http_direct';
  bool _loading = true;

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
        _mode = device.mode;
        _serverUrlCtrl.text = device.serverUrl ?? '';
        setState(() => _loading = false);
      }
    } catch (e) {
      if (mounted) setState(() => _loading = false);
    }
  }

  Future<void> _save() async {
    final api = ref.read(apiServiceProvider);
    await api.updateDevice(widget.deviceId, {
      'name': _nameCtrl.text.trim(),
      'unit': _unitCtrl.text.trim(),
      'upload_interval_ms': _uploadInterval,
      'mode': _mode,
      'server_url': _serverUrlCtrl.text.trim(),
    });
    await ref.read(deviceListProvider.notifier).refresh();
    if (mounted) Navigator.pop(context);
  }

  @override
  void dispose() {
    _nameCtrl.dispose();
    _unitCtrl.dispose();
    _serverUrlCtrl.dispose();
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
          TextField(
            controller: _unitCtrl,
            decoration: const InputDecoration(labelText: 'Weight Unit'),
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
          const Text('Report Mode', style: TextStyle(fontSize: 14, fontWeight: FontWeight.w500)),
          const SizedBox(height: 8),
          SegmentedButton<String>(
            segments: const [
              ButtonSegment(value: 'http_direct', label: Text('HTTP'), icon: Icon(Icons.http, size: 18)),
              ButtonSegment(value: 'mqtt', label: Text('MQTT'), icon: Icon(Icons.router, size: 18)),
              ButtonSegment(value: 'ble_only', label: Text('BLE'), icon: Icon(Icons.bluetooth, size: 18)),
            ],
            selected: {_mode},
            onSelectionChanged: (sel) => setState(() => _mode = sel.first),
          ),
          const SizedBox(height: 12),
          if (_mode == 'http_direct') ...[
            TextField(
              controller: _serverUrlCtrl,
              decoration: InputDecoration(
                labelText: 'Server URL',
                prefixIcon: const Icon(Icons.dns),
                hintText: getDefaultApiBaseUrl(),
              ),
              keyboardType: TextInputType.url,
            ),
          ],
          if (_mode == 'ble_only')
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Colors.blue.shade50,
                borderRadius: BorderRadius.circular(8),
              ),
              child: const Text('BLE-only: no server needed.'),
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
