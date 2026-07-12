import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

/// A locally-saved weight reading — captured by the user tapping the Record button.
/// Survives across app restarts via SharedPreferences.
class SavedMeasurement {
  final String deviceId;
  final double weight;
  final String unit;
  final DateTime recordedAt;
  final String? note;

  const SavedMeasurement({
    required this.deviceId,
    required this.weight,
    required this.unit,
    required this.recordedAt,
    this.note,
  });

  Map<String, dynamic> toJson() => {
        'device_id': deviceId,
        'weight': weight,
        'unit': unit,
        'recorded_at': recordedAt.toIso8601String(),
        'note': note,
      };

  factory SavedMeasurement.fromJson(Map<String, dynamic> json) {
    return SavedMeasurement(
      deviceId: json['device_id'] as String,
      weight: (json['weight'] as num).toDouble(),
      unit: json['unit'] as String? ?? 'g',
      recordedAt: DateTime.parse(json['recorded_at'] as String),
      note: json['note'] as String?,
    );
  }
}

class SavedMeasurementStore {
  static const _kKey = 'saved_measurements';

  /// Persist a new measurement to the top of the list.
  static Future<void> add(SavedMeasurement m) async {
    final prefs = await SharedPreferences.getInstance();
    final all = await allFor(m.deviceId);
    all.insert(0, m);
    await prefs.setString(_kKeyFor(m.deviceId), jsonEncode(all.map((e) => e.toJson()).toList()));
  }

  /// Remove a measurement at the given index.
  static Future<void> removeAt(String deviceId, int index) async {
    final prefs = await SharedPreferences.getInstance();
    final all = await allFor(deviceId);
    if (index < 0 || index >= all.length) return;
    all.removeAt(index);
    await prefs.setString(_kKeyFor(deviceId), jsonEncode(all.map((e) => e.toJson()).toList()));
  }

  /// Get all saved measurements for a device, newest first.
  static Future<List<SavedMeasurement>> allFor(String deviceId) async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_kKeyFor(deviceId));
    if (raw == null || raw.isEmpty) return [];
    try {
      final list = jsonDecode(raw) as List<dynamic>;
      return list
          .map((e) => SavedMeasurement.fromJson(e as Map<String, dynamic>))
          .toList();
    } catch (_) {
      return [];
    }
  }

  /// Clear all locally saved measurements for every device.
  static Future<void> clearAll() async {
    final prefs = await SharedPreferences.getInstance();
    final keys = prefs.getKeys().where((k) => k.startsWith('${_kKey}_')).toList();
    for (final k in keys) {
      await prefs.remove(k);
    }
  }

  static String _kKeyFor(String deviceId) => '${_kKey}_$deviceId';
}
