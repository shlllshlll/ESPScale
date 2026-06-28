import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../config.dart';
import '../providers/app_providers.dart';

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
          ListTile(
            leading: const Icon(Icons.delete_forever, color: Colors.red),
            title: const Text('Clear All Data', style: TextStyle(color: Colors.red)),
            onTap: () {},
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
                // Normalize: strip trailing slash
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
}
