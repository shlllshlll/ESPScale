import 'dart:async';
import 'dart:convert';

import 'package:dio/dio.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../config.dart';
import '../models/device.dart';
import '../models/weight_record.dart';

class ApiService {
  final Dio _dio = Dio(BaseOptions(
    baseUrl: AppConfig.apiBaseUrl,
    connectTimeout: const Duration(seconds: 10),
    receiveTimeout: const Duration(seconds: 15),
    headers: {'Content-Type': 'application/json'},
  ));

  String? _token;
  void setToken(String? token) => _token = token;

  Future<DeviceModel> registerDevice({
    required String deviceId,
    required String apiKey,
    String name = '',
    String firmwareVer = '',
  }) async {
    final res = await _dio.post('/api/v1/devices/register', data: {
      'device_id': deviceId,
      'api_key': apiKey,
      'name': name,
      'firmware_ver': firmwareVer,
    });
    return DeviceModel.fromJson(res.data);
  }

  Future<List<DeviceModel>> fetchDevices() async {
    final res = await _dio.get('/api/v1/devices',
        options: Options(headers: _authHeader()));
    return (res.data as List).map((j) => DeviceModel.fromJson(j)).toList();
  }

  Future<DeviceModel> getDevice(String deviceId) async {
    final res = await _dio.get('/api/v1/devices/$deviceId',
        options: Options(headers: _authHeader()));
    return DeviceModel.fromJson(res.data);
  }

  Future<DeviceModel> updateDevice(String deviceId, Map<String, dynamic> data) async {
    final res = await _dio.put('/api/v1/devices/$deviceId', data: data,
        options: Options(headers: _authHeader()));
    return DeviceModel.fromJson(res.data);
  }

  Future<void> deleteDevice(String deviceId) async {
    await _dio.delete('/api/v1/devices/$deviceId',
        options: Options(headers: _authHeader()));
  }

  Future<List<WeightRecord>> fetchRecords(String deviceId, {int? from, int? to, int limit = 100, int offset = 0}) async {
    final params = <String, dynamic>{'limit': limit, 'offset': offset};
    if (from != null) params['from'] = from;
    if (to != null) params['to'] = to;
    final res = await _dio.get('/api/v1/devices/$deviceId/records', queryParameters: params,
        options: Options(headers: _authHeader()));
    return (res.data as List).map((j) => WeightRecord.fromJson(j)).toList();
  }

  Future<WeightRecord?> fetchLatest(String deviceId) async {
    try {
      final res = await _dio.get('/api/v1/devices/$deviceId/records/latest',
          options: Options(headers: _authHeader()));
      if (res.data == null) return null;
      return WeightRecord.fromJson(res.data);
    } on DioException {
      return null;
    }
  }

  Future<WeightStats> fetchStats(String deviceId, {int? from, int? to}) async {
    final params = <String, dynamic>{};
    if (from != null) params['from'] = from;
    if (to != null) params['to'] = to;
    final res = await _dio.get('/api/v1/devices/$deviceId/records/stats', queryParameters: params,
        options: Options(headers: _authHeader()));
    return WeightStats.fromJson(res.data);
  }

  Map<String, String> _authHeader() => _token != null ? {'Authorization': 'Bearer $_token'} : {};
}

final apiServiceProvider = Provider<ApiService>((ref) => ApiService());
