import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../config.dart';
import '../providers/app_providers.dart';
import '../services/saved_measurement_store.dart';

class AppSettingsScreen extends ConsumerWidget {
  const AppSettingsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final config = ref.watch(serverConfigProvider);

    return Scaffold(
      appBar: AppBar(title: const Text('App Settings')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          config.when(
            data: (cfg) => ListTile(
              leading: const Icon(Icons.dns),
              title: const Text('Server Address'),
              subtitle: Text(cfg.apiBaseUrl),
              trailing: PopupMenuButton<String>(
                onSelected: (action) {
                  if (action == 'edit') {
                    _showEditDialog(context, ref, cfg.apiBaseUrl);
                  } else if (action == 'reset') {
                    ref.read(serverConfigProvider.notifier).resetToDefault();
                  }
                },
                itemBuilder: (_) => [
                  const PopupMenuItem(value: 'edit', child: Text('Edit')),
                  const PopupMenuItem(value: 'reset', child: Text('Reset to Default')),
                ],
              ),
            ),
            error: (e, _) => ListTile(
              leading: const Icon(Icons.dns),
              title: const Text('Server Address'),
              subtitle: const Text('[Error loading config]'),
              onTap: () => ref.invalidate(serverConfigProvider),
            ),
            loading: () => const ListTile(
              leading: Icon(Icons.dns),
              title: Text('Server Address'),
              subtitle: Text('Loading...'),
            ),
          ),
          ListTile(
            leading: const Icon(Icons.info),
            title: const Text('App Version'),
            subtitle: const Text(AppConfig.version),
            onTap: () {},
          ),
          ListTile(
            leading: const Icon(Icons.info_outline),
            title: const Text('Default Server'),
            subtitle: Text('Debug: localhost:8000\nRelease: espscale.shlll.top'),
            onTap: () {},
          ),
          const Divider(),
          config.when(
            data: (cfg) => ListTile(
              leading: const Icon(Icons.key),
              title: const Text('Server API Key'),
              subtitle: Text(cfg.appApiKey.isEmpty ? '(not set — dev mode)' : '*** configured'),
              trailing: PopupMenuButton<String>(
                onSelected: (action) {
                  if (action == 'edit') {
                    _showApiKeyDialog(context, ref, cfg.appApiKey);
                  } else if (action == 'clear') {
                    ref.read(serverConfigProvider.notifier).setApiKey('');
                  }
                },
                itemBuilder: (_) => [
                  const PopupMenuItem(value: 'edit', child: Text('Edit')),
                  const PopupMenuItem(value: 'clear', child: Text('Clear')),
                ],
              ),
            ),
            error: (e, _) => ListTile(
              leading: const Icon(Icons.key),
              title: const Text('Server API Key'),
              subtitle: const Text('[Error loading config]'),
            ),
            loading: () => const ListTile(
              leading: Icon(Icons.key),
              title: Text('Server API Key'),
              subtitle: Text('Loading...'),
            ),
          ),
          const Divider(),
          ListTile(
            leading: const Icon(Icons.delete_forever, color: Colors.red),
            title: const Text('Clear All Data', style: TextStyle(color: Colors.red)),
            onTap: () {
              _showClearDataDialog(context, ref);
            },
          ),
        ],
      ),
    );
  }

  void _showEditDialog(BuildContext context, WidgetRef ref, String currentUrl) {
    final controller = TextEditingController(text: currentUrl);
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Server Address'),
        content: TextField(
          controller: controller,
          decoration: const InputDecoration(
            labelText: 'URL',
            hintText: 'http://localhost:8000',
            prefixIcon: Icon(Icons.link),
          ),
          keyboardType: TextInputType.url,
          autofocus: true,
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () {
              final url = controller.text.trim();
              if (url.isNotEmpty) {
                final normalized = url.endsWith('/') ? url.substring(0, url.length - 1) : url;
                ref.read(serverConfigProvider.notifier).setServerUrl(normalized);
              } else {
                ref.read(serverConfigProvider.notifier).resetToDefault();
              }
              Navigator.pop(ctx);
            },
            child: const Text('Save'),
          ),
        ],
      ),
    );
  }

  void _showApiKeyDialog(BuildContext context, WidgetRef ref, String currentKey) {
    final controller = TextEditingController(text: currentKey);
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Server API Key'),
        content: TextField(
          controller: controller,
          decoration: const InputDecoration(
            labelText: 'API Key',
            hintText: 'Leave empty for dev mode (no auth)',
            prefixIcon: Icon(Icons.key),
          ),
          keyboardType: TextInputType.text,
          autofocus: true,
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () {
              final key = controller.text.trim();
              ref.read(serverConfigProvider.notifier).setApiKey(key);
              Navigator.pop(ctx);
            },
            child: const Text('Save'),
          ),
        ],
      ),
    );
  }

  void _showClearDataDialog(BuildContext context, WidgetRef ref) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Clear All Data'),
        content: const Text('This will clear locally saved measurements and cached data. Server data is not affected.'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          FilledButton(
            style: FilledButton.styleFrom(backgroundColor: Colors.red),
            onPressed: () async {
              await SavedMeasurementStore.clearAll();
              if (context.mounted) Navigator.pop(ctx);
            },
            child: const Text('Clear'),
          ),
        ],
      ),
    );
  }
}
