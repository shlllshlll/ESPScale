import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../models/device.dart';
import '../models/weight_record.dart';
import '../services/api_service.dart';
import '../services/auth_service.dart';
import '../services/ble_service.dart';
import '../services/ws_service.dart';

final deviceListProvider = AsyncNotifierProvider<DeviceListNotifier, List<DeviceModel>>(
  DeviceListNotifier.new,
);

class DeviceListNotifier extends AsyncNotifier<List<DeviceModel>> {
  @override
  Future<List<DeviceModel>> build() async {
    final token = ref.read(authProvider).token;
    if (token == null) return [];
    ref.read(apiServiceProvider).setToken(token);
    return ref.read(apiServiceProvider).fetchDevices();
  }

  Future<void> refresh() async {
    state = const AsyncLoading();
    state = await AsyncValue.guard(() async {
      final token = ref.read(authProvider).token;
      if (token == null) return [];
      ref.read(apiServiceProvider).setToken(token);
      return ref.read(apiServiceProvider).fetchDevices();
    });
  }
}

final bleConnectionProvider = StreamProvider<BluetoothConnectionState>((ref) {
  return ref.read(bleServiceProvider).connectionState;
});

final bleScanResultsProvider = NotifierProvider<BleScanResultsNotifier, List<ScanResult>>(
  BleScanResultsNotifier.new,
);

class BleScanResultsNotifier extends Notifier<List<ScanResult>> {
  @override
  List<ScanResult> build() => [];

  void set(List<ScanResult> results) => state = results;
}

enum WeightSource { ble, ws, none }

final weightSourceProvider = NotifierProvider<WeightSourceNotifier, WeightSource>(
  WeightSourceNotifier.new,
);

class WeightSourceNotifier extends Notifier<WeightSource> {
  @override
  WeightSource build() => WeightSource.none;

  void set(WeightSource source) => state = source;
}

final liveWeightProvider = StreamProvider<WeightReading>((ref) {
  final source = ref.watch(weightSourceProvider);
  if (source == WeightSource.ble) {
    return ref.read(bleServiceProvider).notifyCharacteristic('F5E4D3C2-DD6C-4ED8-9555-3BE20F962A74').map(
          (json) => WeightReading(
            weight: (json['weight'] as num).toDouble(),
            unit: json['unit'] as String? ?? 'g',
            stable: json['stable'] as bool? ?? true,
            seq: json['seq'] as int? ?? 0,
            timestamp: json['timestamp'] as int? ?? 0,
          ),
        );
  } else if (source == WeightSource.ws) {
    return ref.read(wsServiceProvider).weightStream.map(
          (json) => WeightReading(
            weight: (json['weight'] as num).toDouble(),
            unit: json['unit'] as String? ?? 'g',
            stable: json['stable'] as bool? ?? true,
            seq: json['seq'] as int? ?? 0,
            timestamp: json['timestamp'] as int? ?? 0,
          ),
        );
  }
  return const Stream.empty();
});

class WeightReading {
  final double weight;
  final String unit;
  final bool stable;
  final int seq;
  final int timestamp;
  const WeightReading({
    required this.weight,
    this.unit = 'g',
    this.stable = true,
    this.seq = 0,
    this.timestamp = 0,
  });
}
