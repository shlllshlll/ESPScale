import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../providers/app_providers.dart';

class ConnectionIndicator extends ConsumerWidget {
  const ConnectionIndicator({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connState = ref.watch(bleConnectionProvider).value;
    final isBle = connState == BluetoothConnectionState.connected;

    return Tooltip(
      message: isBle ? 'BLE Connected' : 'Not Connected',
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 8),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.bluetooth, size: 18, color: isBle ? Colors.blue : Colors.grey),
            const SizedBox(width: 2),
            Container(
              width: 8,
              height: 8,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: isBle ? Colors.blue : Colors.grey.shade400,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
