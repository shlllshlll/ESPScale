import 'dart:math';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../config.dart';
import '../generated/secrets.dart' show kMqttUser, kMqttPass;
import '../providers/app_providers.dart';
import '../services/api_service.dart';
import '../services/ble_service.dart';

/// Generate a 32-character hex API key (128-bit random).
String _generateApiKey() {
  final rng = Random.secure();
  return List.generate(32, (_) => rng.nextInt(16).toRadixString(16)).join();
}

/// SharedPreferences keys for WiFi credential persistence.
const _kSavedSsid = 'provision_saved_ssid';
const _kSavedPass = 'provision_saved_pass';
const _kSavedMqttHost = 'provision_saved_mqtt_host';
const _kSavedMqttPort = 'provision_saved_mqtt_port';

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
  bool _configLoadedFromDevice = false;
  String _apiKey = '';

  @override
  void initState() {
    super.initState();
    _loadPersistedValues();
  }

  /// Load saved WiFi creds, MQTT config, and server URL from SharedPreferences.
  Future<void> _loadPersistedValues() async {
    final prefs = await SharedPreferences.getInstance();

    // Restore WiFi creds
    final savedSsid = prefs.getString(_kSavedSsid);
    final savedPass = prefs.getString(_kSavedPass);
    if (savedSsid != null && savedSsid.isNotEmpty) {
      _ssidCtrl.text = savedSsid;
    }
    if (savedPass != null) {
      _passCtrl.text = savedPass;
    }

    // Restore MQTT config
    final savedMqttHost = prefs.getString(_kSavedMqttHost);
    if (savedMqttHost != null && savedMqttHost.isNotEmpty) {
      _mqttHostCtrl.text = savedMqttHost;
    }
    final savedMqttPort = prefs.getInt(_kSavedMqttPort);
    if (savedMqttPort != null) {
      _mqttPortCtrl.text = savedMqttPort.toString();
    }

    // Restore server URL
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

  /// Step 0→1: Read device info + full config via get_status command.
  Future<void> _readDeviceInfo() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final ble = ref.read(bleServiceProvider);

      // Read basic device info (device_id, fw_version, mode)
      final info = await ble.readCharacteristic(AppConfig.charDeviceInfo);
      _deviceId = info['device_id'] as String?;
      _fwVersion = info['firmware_version'] as String?;

      // Parse mode from device info
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
            _mode = 2;
          }
        }
      }

      // Try to get full device config via get_status command
      try {
        final status = await ble.getDeviceStatus();
        if (status != null) {
          _configLoadedFromDevice = true;

          // Pre-fill mode from device
          final statusMode = status['mode'];
          if (statusMode is int) {
            _mode = statusMode;
          }

          // Pre-fill server URL from device (only if device has one)
          final deviceServerUrl = status['server_url'] as String? ?? '';
          if (deviceServerUrl.isNotEmpty) {
            _serverUrlCtrl.text = deviceServerUrl;
          }

          // Pre-fill MQTT config from device
          final deviceMqttHost = status['mqtt_host'] as String? ?? '';
          if (deviceMqttHost.isNotEmpty) {
            _mqttHostCtrl.text = deviceMqttHost;
          }
          final deviceMqttPort = status['mqtt_port'];
          if (deviceMqttPort != null && deviceMqttPort is int && deviceMqttPort > 0) {
            _mqttPortCtrl.text = deviceMqttPort.toString();
          }
        }
      } catch (_) {
        // get_status might fail on older firmware — that's OK, use persisted values
      }

      setState(() => _step = 1);
    } catch (e) {
      setState(() => _error = 'Failed to read device info: $e');
    } finally {
      setState(() => _loading = false);
    }
  }

  /// Persist WiFi creds and MQTT config to SharedPreferences.
  Future<void> _saveCredentials() async {
    final prefs = await SharedPreferences.getInstance();
    if (_ssidCtrl.text.trim().isNotEmpty) {
      await prefs.setString(_kSavedSsid, _ssidCtrl.text.trim());
      await prefs.setString(_kSavedPass, _passCtrl.text.trim());
    }
    if (_mqttHostCtrl.text.trim().isNotEmpty) {
      await prefs.setString(_kSavedMqttHost, _mqttHostCtrl.text.trim());
      await prefs.setInt(_kSavedMqttPort, int.tryParse(_mqttPortCtrl.text.trim()) ?? 1883);
    }
  }

  /// Step 1→2: Send provisioning data to device.
  Future<void> _provision() async {
    final ssid = _ssidCtrl.text.trim();
    final pass = _passCtrl.text.trim();

    // WiFi is required for HTTP and MQTT modes, not for BLE-only
    if (_mode != 2 && ssid.isEmpty) {
      setState(() => _error = 'WiFi SSID is required for HTTP and MQTT modes');
      return;
    }

    // Generate API key for device authentication with server
    _apiKey = _generateApiKey();

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
      mqttUser: kMqttUser,
      mqttPass: kMqttPass,
    );

    if (!mounted) return;
    if (result.success) {
      // Save credentials for next time
      await _saveCredentials();

      // Send the API key to firmware via set_config so it can authenticate with server
      try {
        await ble.sendCommand('set_config', {
          'api_key': _apiKey,
        }, 'apikey-${DateTime.now().millisecondsSinceEpoch}');
      } catch (_) {
        // Non-fatal — firmware will have empty API key, HTTP auth will fail
        // but the device is still usable via BLE
      }

      if (mounted) {
        setState(() {
          _step = 3;
          _loading = false;
        });
      }
    } else if (mounted) {
      setState(() {
        _error = result.error;
        _step = 1;
        _loading = false;
      });
    }
  }

  /// Step 3→finish: Register device on server and navigate to detail page.
  Future<void> _finish() async {
    final api = ref.read(apiServiceProvider);
    String? regError;
    try {
      if (_deviceId != null && _apiKey.isNotEmpty) {
        await api.registerDevice(deviceId: _deviceId!, apiKey: _apiKey, firmwareVer: _fwVersion ?? '');
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
      // Pop the provision screen off the stack, then push detail so back returns to device list
      Navigator.of(context).pushNamedAndRemoveUntil(
        '/devices',
        (route) => false,
      );
      Navigator.of(context).pushNamed('/device', arguments: _deviceId);
    } else {
      Navigator.of(context).pushNamedAndRemoveUntil('/devices', (_) => false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Provision Scale')),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(24),
        child: _loading
            ? const Padding(
                padding: EdgeInsets.only(top: 80),
                child: Center(child: Column(mainAxisSize: MainAxisSize.min, children: [
                  CircularProgressIndicator(),
                  SizedBox(height: 16),
                  Text('Working...'),
                ])),
              )
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
                    // --- Device info card ---
                    if (_deviceId != null)
                      Card(
                        child: Padding(
                          padding: const EdgeInsets.all(16),
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text('Device: $_deviceId', style: Theme.of(context).textTheme.titleMedium),
                              if (_fwVersion != null) Text('Firmware: $_fwVersion'),
                              if (_configLoadedFromDevice)
                                Text('Config loaded from device',
                                    style: TextStyle(color: Colors.green.shade700, fontSize: 12)),
                            ],
                          ),
                        ),
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
                    const SizedBox(height: 16),

                    // --- WiFi credentials (only for HTTP and MQTT modes) ---
                    if (_mode != 2) ...[
                      Text('WiFi Configuration', style: Theme.of(context).textTheme.titleSmall),
                      const SizedBox(height: 8),
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
                    ],

                    // --- Mode-specific config ---
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
                        child: const Text(
                            'BLE-only mode: no WiFi or server needed. Weight data streams directly to this app via Bluetooth.'),
                      ),
                    ],
                    const SizedBox(height: 24),
                    ElevatedButton.icon(
                      onPressed: _provision,
                      icon: const Icon(Icons.play_arrow),
                      label: Text(_mode == 2 ? 'Configure & Connect' : 'Provision'),
                      style: ElevatedButton.styleFrom(minimumSize: const Size.fromHeight(48)),
                    ),
                  ],
                  if (_step == 3) ...[
                    const Icon(Icons.check_circle, size: 80, color: Colors.green),
                    const SizedBox(height: 16),
                    Text(
                      _mode == 2 ? 'Configuration applied!' : 'Provisioning successful!',
                      textAlign: TextAlign.center,
                      style: const TextStyle(fontSize: 18),
                    ),
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
