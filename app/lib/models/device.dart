class DeviceModel {
  final String deviceId;
  final String name;
  final bool isOnline;
  final double? lastWeight;
  final double calFactor;
  final String unit;
  final String mode;
  final String firmwareVer;
  final int uploadIntervalMs;
  final DateTime? lastSeen;
  final DateTime createdAt;
  final DateTime updatedAt;

  const DeviceModel({
    required this.deviceId,
    required this.name,
    this.isOnline = false,
    this.lastWeight,
    this.calFactor = 397.6,
    this.unit = 'g',
    this.mode = 'remote',
    this.firmwareVer = '',
    this.uploadIntervalMs = 5000,
    this.lastSeen,
    required this.createdAt,
    required this.updatedAt,
  });

  factory DeviceModel.fromJson(Map<String, dynamic> json) {
    return DeviceModel(
      deviceId: json['device_id'] as String,
      name: json['name'] as String? ?? '',
      isOnline: json['is_online'] as bool? ?? false,
      lastWeight: (json['last_weight'] as num?)?.toDouble(),
      calFactor: (json['cal_factor'] as num?)?.toDouble() ?? 397.6,
      unit: json['unit'] as String? ?? 'g',
      mode: json['mode'] as String? ?? 'remote',
      firmwareVer: json['firmware_ver'] as String? ?? '',
      uploadIntervalMs: json['upload_interval_ms'] as int? ?? 5000,
      lastSeen: json['last_seen'] != null ? DateTime.tryParse(json['last_seen']) : null,
      createdAt: DateTime.tryParse(json['created_at'] ?? '') ?? DateTime.now(),
      updatedAt: DateTime.tryParse(json['updated_at'] ?? '') ?? DateTime.now(),
    );
  }

  Map<String, dynamic> toJson() => {
        'device_id': deviceId,
        'name': name,
        'is_online': isOnline,
        'last_weight': lastWeight,
        'cal_factor': calFactor,
        'unit': unit,
        'mode': mode,
        'firmware_ver': firmwareVer,
        'upload_interval_ms': uploadIntervalMs,
      };
}
