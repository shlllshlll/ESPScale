import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../services/ble_service.dart';
import '../services/api_service.dart';
import '../models/device.dart';

class CalibrationScreen extends ConsumerStatefulWidget {
  final String deviceId;
  const CalibrationScreen({super.key, required this.deviceId});

  @override
  ConsumerState<CalibrationScreen> createState() => _CalibrationScreenState();
}

class _CalibrationScreenState extends ConsumerState<CalibrationScreen> {
  final _weightCtrl = TextEditingController(text: '500');
  bool _loading = false;
  String? _result;

  Future<void> _calibrate() async {
    final expectedWeight = double.tryParse(_weightCtrl.text.trim());
    if (expectedWeight == null || expectedWeight <= 0) {
      setState(() => _result = 'Please enter a valid weight');
      return;
    }

    setState(() {
      _loading = true;
      _result = null;
    });

    try {
      final ble = ref.read(bleServiceProvider);
      await ble.sendCommand('calibrate', {'expected_weight': expectedWeight},
          DateTime.now().millisecondsSinceEpoch.toRadixString(16));
      setState(() => _result = 'Calibration complete! Place $expectedWeight g and calibrate.');
    } catch (e) {
      setState(() => _result = 'Error: $e');
    } finally {
      setState(() => _loading = false);
    }
  }

  @override
  void dispose() {
    _weightCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Calibration')),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: _loading
            ? const Center(child: CircularProgressIndicator())
            : Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  const Text('Step 1: Clear the scale platform', style: TextStyle(fontSize: 16)),
                  const SizedBox(height: 8),
                  ElevatedButton(onPressed: () => _calibrate, child: const Text('Send Tare')),
                  const Divider(height: 32),
                  const Text('Step 2: Place known weight and enter value', style: TextStyle(fontSize: 16)),
                  const SizedBox(height: 12),
                  TextField(
                    controller: _weightCtrl,
                    decoration: const InputDecoration(labelText: 'Known weight (g)', suffixText: 'g'),
                    keyboardType: TextInputType.number,
                  ),
                  const SizedBox(height: 16),
                  ElevatedButton.icon(
                    onPressed: _calibrate,
                    icon: const Icon(Icons.tune),
                    label: const Text('Calibrate'),
                    style: ElevatedButton.styleFrom(minimumSize: const Size.fromHeight(48)),
                  ),
                  if (_result != null) ...[
                    const SizedBox(height: 16),
                    Card(
                      child: Padding(
                        padding: const EdgeInsets.all(16),
                        child: Text(_result!, style: const TextStyle(fontSize: 16)),
                      ),
                    ),
                  ],
                ],
              ),
      ),
    );
  }
}
