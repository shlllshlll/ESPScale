import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../config.dart';
import '../providers/app_providers.dart';
import '../services/api_service.dart';
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
  final _serverUrlCtrl = TextEditingController();
  final _mqttHostCtrl = TextEditingController();
  final _mqttPortCtrl = TextEditingController(text: '1883');
  int _mode = 0; // 0=HTTP Direct, 1=MQTT, 2=BLE Only
  bool _loading = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    final config = ref.read(serverConfigProvider);
    _serverUrlCtrl.text = config.value?.apiBaseUrl ?? getDefaultApiBaseUrl();
  }

  @override
  void dispose() {
    _ssidCtrl.dispose();
    _passCtrl.dispose();
    _serverUrlCtrl.dispose();
    _mqttHostCtrl.dispose();
    _mqttPortCtrl.dispose();
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
        // Read current mode from device info — can be int (new fw) or string (old fw)
        final devMode = info['mode'];
        if (devMode != null) {
          if (devMode is int) {
            _mode = devMode;
          } else {
            final s = devMode.toString();
            if (s == 'remote' || s == 'http_direct') {
              _mode = 0;
            } else if (s == 'mqtt') {
              _mode = 1;
            } else {
              _mode = 2; // ble_only / local
            }
          }
        }
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
    final result = await ble.provisionDevice(
      ssid: ssid,
      password: pass,
      mode: _mode,
      serverUrl: _serverUrlCtrl.text.trim(),
      mqttHost: _mqttHostCtrl.text.trim(),
      mqttPort: int.tryParse(_mqttPortCtrl.text.trim()) ?? 1883,
    );

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
    String? regError;
    try {
      if (_deviceId != null) {
        await api.registerDevice(deviceId: _deviceId!, apiKey: 'placeholder-key', firmwareVer: _fwVersion ?? '');
      }
    } catch (e) {
      regError = e.toString();
    }

    try {
      await ref.read(deviceListProvider.notifier).refresh();
    } catch (_) {}

    if (!mounted) return;
    if (regError != null && _deviceId == null) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Register failed: $regError')));
    }
    if (_deviceId != null) {
      Navigator.of(context).pushNamedAndRemoveUntil('/device', (_) => false, arguments: _deviceId);
    } else {
      Navigator.of(context).pushNamedAndRemoveUntil('/devices', (_) => false);
    }
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
                  if (_step == 0) ...[
                    ElevatedButton.icon(
                      onPressed: _readDeviceInfo,
                      icon: const Icon(Icons.info),
                      label: const Text('Read Device Info'),
                      style: ElevatedButton.styleFrom(minimumSize: const Size.fromHeight(48)),
                    ),
                  ],
                  if (_step >= 1) ...[
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
                    const SizedBox(height: 16),
                    // --- Mode selector ---
                    Text('Report Mode', style: Theme.of(context).textTheme.titleSmall),
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
                    const SizedBox(height: 12),
                    // --- Conditional config fields ---
                    if (_mode == 0) ...[
                      TextField(
                        controller: _serverUrlCtrl,
                        decoration: const InputDecoration(
                          labelText: 'Server URL',
                          prefixIcon: Icon(Icons.dns),
                          hintText: 'http://localhost:8000',
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
                                hintText: 'localhost',
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
                    if (_mode == 2) ...[
                      Container(
                        padding: const EdgeInsets.all(12),
                        decoration: BoxDecoration(
                          color: Colors.blue.shade50,
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: const Text('BLE-only mode: no server needed. Weight data streams directly to this app via Bluetooth.'),
                      ),
                    ],
                    const SizedBox(height: 24),
                    ElevatedButton.icon(
                      onPressed: _provision,
                      icon: const Icon(Icons.play_arrow),
                      label: const Text('Provision'),
                      style: ElevatedButton.styleFrom(minimumSize: const Size.fromHeight(48)),
                    ),
                  ],
                  if (_step == 3) ...[
                    const Icon(Icons.check_circle, size: 80, color: Colors.green),
                    const SizedBox(height: 16),
                    const Text('Provisioning successful!', textAlign: TextAlign.center, style: TextStyle(fontSize: 18)),
                    const SizedBox(height: 24),
                    ElevatedButton.icon(
                      onPressed: _finish,
                      icon: const Icon(Icons.arrow_forward),
                      label: const Text('Go to Device'),
                      style: ElevatedButton.styleFrom(minimumSize: const Size.fromHeight(48)),
                    ),
                  ],
                ],
              ),
      ),
    );
  }
}
