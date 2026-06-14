import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../config.dart';
import '../models/device.dart';
import '../models/weight_record.dart';
import '../providers/app_providers.dart';
import '../services/api_service.dart';
import '../services/ble_service.dart';
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
  String? _error;

  @override
  void initState() {
    super.initState();
    _load();
    _connectWs();
  }

  Future<void> _load() async {
    try {
      final api = ref.read(apiServiceProvider);
      final device = await api.getDevice(widget.deviceId);
      final records = await api.fetchRecords(widget.deviceId, limit: 200);
      if (mounted) {
        setState(() {
          _device = device;
          _records = records;
          _loading = false;
        });
      }
    } catch (e) {
      if (mounted) setState(() { _error = e.toString(); _loading = false; });
    }
  }

  void _connectWs() {
    final ws = ref.read(wsServiceProvider);
    final token = ref.read(authProvider).token;
    if (token != null) {
      ws.setToken(token);
      ws.connect(widget.deviceId);
      ref.read(weightSourceProvider.notifier).state = WeightSource.ws;
    }
  }

  @override
  void dispose() {
    ref.read(weightSourceProvider.notifier).state = WeightSource.none;
    super.dispose();
  }

  Future<void> _sendCommand(String cmd, {Map<String, dynamic> params = const {}}) async {
    final ble = ref.read(bleServiceProvider);
    await ble.sendCommand(cmd, params, DateTime.now().millisecondsSinceEpoch.toRadixString(16));
    await _load();
  }

  @override
  Widget build(BuildContext context) {
    final weightReading = ref.watch(liveWeightProvider).valueOrNull;

    return Scaffold(
      appBar: AppBar(
        title: Text(_device?.name ?? widget.deviceId),
        actions: [
          const ConnectionIndicator(),
          IconButton(
            icon: const Icon(Icons.settings),
            onPressed: () => Navigator.of(context).pushNamed('/settings', arguments: widget.deviceId),
          ),
        ],
      ),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : _error != null
              ? Center(child: Text('Error: $_error'))
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
                      SizedBox(
                        height: 220,
                        child: WeightChart(records: _records),
                      ),
                      const SizedBox(height: 16),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                        children: [
                          _ActionButton(icon: Icons.balance, label: 'Tare', onTap: () => _sendCommand('tare')),
                          _ActionButton(icon: Icons.tune, label: 'Calibrate', onTap: () => Navigator.of(context).pushNamed('/calibrate', arguments: widget.deviceId)),
                          _ActionButton(icon: Icons.swap_horiz, label: 'Mode', onTap: () {
                            final newMode = _device?.mode == 'local' ? 'remote' : 'local';
                            _sendCommand('set_mode', params: {'mode': newMode}).then((_) => _load());
                          }),
                        ],
                      ),
                    ],
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
