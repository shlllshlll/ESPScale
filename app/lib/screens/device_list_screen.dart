import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../providers/app_providers.dart';
import '../services/ble_service.dart';
import '../widgets/ble_scan_list.dart';
import '../widgets/device_card.dart';

class DeviceListScreen extends ConsumerStatefulWidget {
  const DeviceListScreen({super.key});

  @override
  ConsumerState<DeviceListScreen> createState() => _DeviceListScreenState();
}

class _DeviceListScreenState extends ConsumerState<DeviceListScreen>
    with SingleTickerProviderStateMixin {
  late TabController _tabCtrl;

  @override
  void initState() {
    super.initState();
    _tabCtrl = TabController(length: 2, vsync: this);
  }

  @override
  void dispose() {
    _tabCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final devicesAsync = ref.watch(deviceListProvider);
    final scanResults = ref.watch(bleScanResultsProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('My Scales'),
        actions: [
          IconButton(
            icon: const Icon(Icons.dns),
            tooltip: 'Server',
            onPressed: () => Navigator.of(context).pushNamed('/app-settings'),
          ),
          IconButton(
            icon: const Icon(Icons.refresh),
            onPressed: () => ref.read(deviceListProvider.notifier).refresh(),
          ),
        ],
        bottom: TabBar(controller: _tabCtrl, tabs: const [
          Tab(text: 'Remote'),
          Tab(text: 'Local (BLE)'),
        ]),
      ),
      body: TabBarView(controller: _tabCtrl, children: [
        devicesAsync.when(
          loading: () => const Center(child: CircularProgressIndicator()),
          error: (e, _) => Center(child: Text('Error: $e')),
          data: (devices) => devices.isEmpty
              ? const Center(child: Text('No devices registered — use the Local (BLE) tab to pair a new scale'))
              : ListView.builder(
                  itemCount: devices.length,
                  itemBuilder: (_, i) => DeviceCard(
                    device: devices[i],
                    onTap: () => Navigator.of(context).pushNamed('/device', arguments: devices[i].deviceId),
                  ),
                ),
        ),
        BLEPanel(),
      ]),
    );
  }
}

class BLEPanel extends ConsumerStatefulWidget {
  const BLEPanel({super.key});

  @override
  ConsumerState<BLEPanel> createState() => _BLEPanelState();
}

class _BLEPanelState extends ConsumerState<BLEPanel> {
  StreamSubscription<List<ScanResult>>? _scanSub;
  StreamSubscription<BluetoothAdapterState>? _adapterSub;
  BluetoothAdapterState _adapterState = BluetoothAdapterState.unknown;

  @override
  void initState() {
    super.initState();
    _adapterState = FlutterBluePlus.adapterStateNow;
    _adapterSub = FlutterBluePlus.adapterState.listen((state) {
      setState(() => _adapterState = state);
      if (state == BluetoothAdapterState.on) _startScan();
    });
    if (_adapterState == BluetoothAdapterState.on) _startScan();
  }

  void _startScan() {
    _scanSub?.cancel();
    _scanSub = ref.read(bleServiceProvider).scanForScales().listen((results) {
      if (mounted) ref.read(bleScanResultsProvider.notifier).set(results);
    });
  }

  @override
  void dispose() {
    _scanSub?.cancel();
    _adapterSub?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (_adapterState == BluetoothAdapterState.unauthorized) {
      return const Center(child: Text('Bluetooth permission denied'));
    }
    if (_adapterState == BluetoothAdapterState.unknown) {
      return const Center(child: CircularProgressIndicator());
    }
    if (_adapterState != BluetoothAdapterState.on) {
      return const Center(child: Text('Bluetooth adapter is off'));
    }
    final results = ref.watch(bleScanResultsProvider);
    if (results.isEmpty) {
      return const Center(child: Text('No nearby ESPScale devices'));
    }
    return BleScanList(
      results: results,
      onConnect: (deviceId) async {
        final ble = ref.read(bleServiceProvider);
        await ble.connect(deviceId);
        if (mounted) Navigator.of(context).pushNamed('/provision');
      },
    );
  }
}
