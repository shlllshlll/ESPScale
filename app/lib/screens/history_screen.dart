import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../services/saved_measurement_store.dart';

/// Displays all measurements the user has captured locally (via the Record button).
/// Works in both online and offline (BLE-only) modes since data is local.
class HistoryScreen extends ConsumerStatefulWidget {
  final String deviceId;
  const HistoryScreen({super.key, required this.deviceId});

  @override
  ConsumerState<HistoryScreen> createState() => _HistoryScreenState();
}

class _HistoryScreenState extends ConsumerState<HistoryScreen> {
  List<SavedMeasurement> _items = [];
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    _refresh();
  }

  Future<void> _refresh() async {
    final items = await SavedMeasurementStore.allFor(widget.deviceId);
    if (mounted) {
      setState(() {
        _items = items;
        _loading = false;
      });
    }
  }

  Future<void> _delete(int index) async {
    final removed = _items[index];
    final confirm = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Delete Record'),
        content: Text('Delete record of ${removed.weight.toStringAsFixed(2)} ${removed.unit}?'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Delete')),
        ],
      ),
    );
    if (confirm == true) {
      await SavedMeasurementStore.removeAt(widget.deviceId, index);
      await _refresh();
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Measurement History')),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : _items.isEmpty
              ? Center(
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(Icons.history, size: 64, color: Colors.grey.shade400),
                      const SizedBox(height: 16),
                      Text(
                        'No recorded measurements yet',
                        style: TextStyle(color: Colors.grey.shade600),
                      ),
                      const SizedBox(height: 8),
                      Text(
                        'Tap the Record button on the device page to save the current weight',
                        style: TextStyle(color: Colors.grey.shade500, fontSize: 12),
                        textAlign: TextAlign.center,
                      ),
                    ],
                  ),
                )
              : RefreshIndicator(
                  onRefresh: _refresh,
                  child: ListView.separated(
                    itemCount: _items.length,
                    separatorBuilder: (_, __) => const Divider(height: 1),
                    itemBuilder: (_, i) {
                      final m = _items[i];
                      return ListTile(
                        leading: CircleAvatar(
                          backgroundColor: Theme.of(context).colorScheme.primaryContainer,
                          child: Icon(Icons.scale, color: Theme.of(context).colorScheme.primary),
                        ),
                        title: Text(
                          '${m.weight.toStringAsFixed(m.weight < 10 ? 2 : 1)} ${m.unit}',
                          style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w500),
                        ),
                        subtitle: Text(_formatTimestamp(m.recordedAt)),
                        trailing: IconButton(
                          icon: const Icon(Icons.delete_outline),
                          onPressed: () => _delete(i),
                        ),
                      );
                    },
                  ),
                ),
    );
  }

  String _formatTimestamp(DateTime t) {
    final now = DateTime.now();
    final diff = now.difference(t);
    if (diff.inSeconds < 60) return 'Just now';
    if (diff.inMinutes < 60) return '${diff.inMinutes} min ago';
    if (diff.inHours < 24) return '${diff.inHours}h ago — ${_abs(t)}';
    if (diff.inDays < 7) return '${diff.inDays}d ago — ${_abs(t)}';
    return _abs(t);
  }

  String _abs(DateTime t) {
    String two(int n) => n.toString().padLeft(2, '0');
    return '${t.year}-${two(t.month)}-${two(t.day)} ${two(t.hour)}:${two(t.minute)}';
  }
}
