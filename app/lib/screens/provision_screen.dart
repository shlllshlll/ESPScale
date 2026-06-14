import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../config.dart';
import '../providers/app_providers.dart';
import '../services/ble_service.dart';

class ProvisionScreen extends ConsumerStatefulWidget {
  const ProvisionScreen({super.key});

  @override
  ConsumerState<ProvisionScreen> createState() => _ProvisionScreenState();
}

class _ProvisionScreenState extends ConsumerState<ProvisionScreen> {
  int _step = 0;
  String? _deviceId;
  String? _fwVersion;
  final _ssidCtrl = TextEditingController();
  final _passCtrl = TextEditingController();
  bool _loading = false;
  String? _error;

  @override
  void dispose() {
    _ssidCtrl.dispose();
    _passCtrl.dispose();
    super.dispose();
  }

  Future<void> _readDeviceInfo() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final ble = ref.read(bleServiceProvider);
      final info = await ble.readCharacteristic(AppConfig.charDeviceInfo);
      setState(() {
        _deviceId = info['device_id'] as String?;
        _fwVersion = info['firmware_version'] as String?;
        _step = 1;
      });
    } catch (e) {
      setState(() => _error = 'Failed to read device info: $e');
    } finally {
      setState(() => _loading = false);
    }
  }

  Future<void> _provision() async {
    final ssid = _ssidCtrl.text.trim();
    final pass = _passCtrl.text.trim();
    if (ssid.isEmpty) {
      setState(() => _error = 'SSID is required');
      return;
    }

    setState(() {
      _loading = true;
      _error = null;
      _step = 2;
    });

    final ble = ref.read(bleServiceProvider);
    final result = await ble.provisionDevice(ssid: ssid, password: pass);

    if (mounted) {
      if (result.success) {
        setState(() => _step = 3);
      } else {
        setState(() {
          _error = result.error;
          _step = 1;
        });
      }
    }
    setState(() => _loading = false);
  }

  Future<void> _finish() async {
    final api = ref.read(apiServiceProvider);
    try {
      if (_deviceId != null) {
        await api.registerDevice(deviceId: _deviceId!, apiKey: 'placeholder-key', firmwareVer: _fwVersion ?? '');
      }
      await ref.read(deviceListProvider.notifier).refresh();
    } catch (_) {}
    if (mounted) Navigator.of(context).pushReplacementNamed('/devices');
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Provision Scale')),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: _loading
            ? const Center(child: Column(mainAxisSize: MainAxisSize.min, children: [
                CircularProgressIndicator(),
                SizedBox(height: 16),
                Text('Working...'),
              ]))
            : Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  if (_error != null)
                    Container(
                      padding: const EdgeInsets.all(12),
                      margin: const EdgeInsets.only(bottom: 16),
                      decoration: BoxDecoration(
                        color: Colors.red.shade50,
                        borderRadius: BorderRadius.circular(8),
                      ),
                      child: Text(_error!, style: TextStyle(color: Colors.red.shade800)),
                    ),
                  if (_step == 0) ...[  // Read device info
                    ElevatedButton.icon(
                      onPressed: _readDeviceInfo,
                      icon: const Icon(Icons.info),
                      label: const Text('Read Device Info'),
                      style: ElevatedButton.styleFrom(minimumSize: const Size.fromHeight(48)),
                    ),
                  ],
                  if (_step >= 1) ...[  // WiFi form
                    if (_deviceId != null)
                      Card(
                        child: Padding(
                          padding: const EdgeInsets.all(16),
                          child: Column(
                            children: [
                              Text('Device: $_deviceId', style: Theme.of(context).textTheme.titleMedium),
                              if (_fwVersion != null) Text('Firmware: $_fwVersion'),
                            ],
                          ),
                        ),
                      ),
                    const SizedBox(height: 16),
                    TextField(
                      controller: _ssidCtrl,
                      decoration: const InputDecoration(labelText: 'WiFi SSID', prefixIcon: Icon(Icons.wifi)),
                    ),
                    const SizedBox(height: 12),
                    TextField(
                      controller: _passCtrl,
                      decoration: const InputDecoration(labelText: 'WiFi Password', prefixIcon: Icon(Icons.lock)),
                      obscureText: true,
                    ),
                    const SizedBox(height: 24),
                    ElevatedButton.icon(
                      onPressed: _provision,
                      icon: const Icon(Icons.play_arrow),
                      label: const Text('Provision'),
                      style: ElevatedButton.styleFrom(minimumSize: const Size.fromHeight(48)),
                    ),
                  ],
                  if (_step == 3) ...[  // Done
                    const Icon(Icons.check_circle, size: 80, color: Colors.green),
                    const SizedBox(height: 16),
                    const Text('Provisioning successful!', textAlign: TextAlign.center, style: TextStyle(fontSize: 18)),
                    const SizedBox(height: 24),
                    ElevatedButton.icon(
                      onPressed: _finish,
                      icon: const Icon(Icons.arrow_forward),
                      label: const Text('Go to Devices'),
                      style: ElevatedButton.styleFrom(minimumSize: const Size.fromHeight(48)),
                    ),
                  ],
                ],
              ),
      ),
    );
  }
}
