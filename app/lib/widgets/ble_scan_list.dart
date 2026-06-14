import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class BleScanList extends StatelessWidget {
  final List<ScanResult> results;
  final void Function(String deviceId) onConnect;

  const BleScanList({super.key, required this.results, required this.onConnect});

  @override
  Widget build(BuildContext context) {
    return ListView.builder(
      itemCount: results.length,
      itemBuilder: (_, i) {
        final r = results[i];
        final name = r.device.platformName;
        final rssi = r.rssi;
        return Card(
          margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
          child: ListTile(
            leading: Icon(Icons.bluetooth, color: rssi > -60 ? Colors.blue : Colors.grey),
            title: Text(name),
            subtitle: Text('RSSI: $rssi dBm'),
            trailing: FilledButton.tonal(
              onPressed: () => onConnect(r.device.remoteId.str),
              child: const Text('Connect'),
            ),
          ),
        );
      },
    );
  }
}
