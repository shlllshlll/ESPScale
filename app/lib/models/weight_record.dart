class WeightRecord {
  final int id;
  final String deviceId;
  final double weight;
  final String unit;
  final int? rawValue;
  final bool stable;
  final int? seq;
  final DateTime timestamp;
  final DateTime receivedAt;

  const WeightRecord({
    required this.id,
    required this.deviceId,
    required this.weight,
    this.unit = 'g',
    this.rawValue,
    this.stable = true,
    this.seq,
    required this.timestamp,
    required this.receivedAt,
  });

  factory WeightRecord.fromJson(Map<String, dynamic> json) {
    return WeightRecord(
      id: json['id'] as int,
      deviceId: json['device_id'] as String,
      weight: (json['weight'] as num).toDouble(),
      unit: json['unit'] as String? ?? 'g',
      rawValue: json['raw_value'] as int?,
      stable: json['stable'] as bool? ?? true,
      seq: json['sequence_number'] as int?,
      timestamp: DateTime.parse(json['timestamp']),
      receivedAt: DateTime.parse(json['received_at']),
    );
  }
}

class WeightStats {
  final double? min;
  final double? max;
  final double? avg;
  final int count;

  const WeightStats({this.min, this.max, this.avg, required this.count});

  factory WeightStats.fromJson(Map<String, dynamic> json) {
    return WeightStats(
      min: (json['min'] as num?)?.toDouble(),
      max: (json['max'] as num?)?.toDouble(),
      avg: (json['avg'] as num?)?.toDouble(),
      count: json['count'] as int? ?? 0,
    );
  }
}
