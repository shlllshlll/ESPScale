import 'package:flutter/material.dart';

import '../config.dart';
import '../models/device.dart';

class DeviceCard extends StatelessWidget {
  final DeviceModel device;
  final VoidCallback? onTap;

  const DeviceCard({super.key, required this.device, this.onTap});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
      child: ListTile(
        leading: Stack(
          children: [
            const Icon(Icons.scale, size: 36),
            Positioned(
              right: 0,
              bottom: 0,
              child: Container(
                width: 12,
                height: 12,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: device.isOnline ? Colors.green : Colors.grey,
                  border: Border.all(color: Colors.white, width: 2),
                ),
              ),
            ),
          ],
        ),
        title: Text(device.name, style: theme.textTheme.titleMedium),
        subtitle: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(device.deviceId, style: theme.textTheme.bodySmall),
            if (device.lastWeight != null)
              Text('${device.lastWeight!.toStringAsFixed(1)} ${device.unit}',
                  style: theme.textTheme.bodyMedium),
          ],
        ),
        trailing: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          crossAxisAlignment: CrossAxisAlignment.end,
          children: [
            Text(device.mode == 'local' ? 'BLE' : 'WiFi',
                style: theme.textTheme.labelSmall?.copyWith(
                  color: theme.colorScheme.primary,
                )),
            if (device.lastSeen != null)
              Text(_fmt(device.lastSeen!), style: theme.textTheme.labelSmall),
          ],
        ),
        onTap: onTap,
      ),
    );
  }

  String _fmt(DateTime dt) {
    final now = DateTime.now();
    final diff = now.difference(dt);
    if (diff.inMinutes < 1) return 'just now';
    if (diff.inMinutes < 60) return '${diff.inMinutes}m ago';
    if (diff.inHours < 24) return '${diff.inHours}h ago';
    return '${diff.inDays}d ago';
  }
}
