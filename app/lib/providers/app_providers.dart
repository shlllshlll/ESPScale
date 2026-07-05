import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../config.dart';
import '../models/device.dart';
import '../services/api_service.dart';
import '../services/ble_service.dart';
import '../services/ws_service.dart';

// --- Server Config ---

final serverConfigProvider = AsyncNotifierProvider<ServerConfigNotifier, ServerConfig>(
  ServerConfigNotifier.new,
);

class ServerConfigNotifier extends AsyncNotifier<ServerConfig> {
  static const String _key = 'server_url_override';

  @override
  Future<ServerConfig> build() async {
    final prefs = await SharedPreferences.getInstance();
    final override = prefs.getString(_key);
    if (override != null && override.isNotEmpty) {
      return ServerConfig(
        apiBaseUrl: override,
        wsBaseUrl: httpToWsUrl(override),
      );
    }
    return ServerConfig(
      apiBaseUrl: getDefaultApiBaseUrl(),
      wsBaseUrl: httpToWsUrl(getDefaultApiBaseUrl()),
    );
  }

  Future<void> setServerUrl(String url) async {
    final prefs = await SharedPreferences.getInstance();
    if (url.isEmpty) {
      await prefs.remove(_key);
      state = AsyncValue.data(ServerConfig(
        apiBaseUrl: getDefaultApiBaseUrl(),
        wsBaseUrl: httpToWsUrl(getDefaultApiBaseUrl()),
      ));
    } else {
      await prefs.setString(_key, url);
      state = AsyncValue.data(ServerConfig(
        apiBaseUrl: url,
        wsBaseUrl: httpToWsUrl(url),
      ));
    }
  }

  Future<void> resetToDefault() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_key);
    state = AsyncValue.data(ServerConfig(
      apiBaseUrl: getDefaultApiBaseUrl(),
      wsBaseUrl: httpToWsUrl(getDefaultApiBaseUrl()),
    ));
  }
}

// --- Device List ---

final deviceListProvider = AsyncNotifierProvider<DeviceListNotifier, List<DeviceModel>>(
  DeviceListNotifier.new,
);

class DeviceListNotifier extends AsyncNotifier<List<DeviceModel>> {
  @override
  Future<List<DeviceModel>> build() async {
    return ref.read(apiServiceProvider).fetchDevices();
  }

  Future<void> refresh() async {
    state = const AsyncLoading();
    state = await AsyncValue.guard(() async {
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

final liveWeightProvider = StreamProvider.autoDispose<WeightReading>((ref) {
  final source = ref.watch(weightSourceProvider);
  print('[liveWeightProvider] source=$source building stream...');
  if (source == WeightSource.ble) {
    print('[liveWeightProvider] subscribing to BLE weight stream ${AppConfig.charWeightStream}');
    return ref.read(bleServiceProvider).notifyCharacteristic(AppConfig.charWeightStream).map(
          (json) {
            print('[liveWeightProvider] BLE weight data: $json');
            return WeightReading(
              weight: (json['weight'] as num).toDouble(),
              unit: json['unit'] as String? ?? 'g',
              stable: json['stable'] as bool? ?? true,
              seq: json['seq'] as int? ?? 0,
              timestamp: json['timestamp'] as int? ?? 0,
            );
          },
        );
  } else if (source == WeightSource.ws) {
    print('[liveWeightProvider] subscribing to WS weight stream');
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
  print('[liveWeightProvider] source=none, returning empty stream');
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
