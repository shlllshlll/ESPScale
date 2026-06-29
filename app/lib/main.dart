import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'screens/calibration_screen.dart';
import 'screens/device_detail_screen.dart';
import 'screens/device_list_screen.dart';
import 'screens/device_settings_screen.dart';
import 'screens/history_screen.dart';
import 'screens/provision_screen.dart';
import 'screens/settings_screen.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const ProviderScope(child: ESPScaleApp()));
}

class ESPScaleApp extends StatelessWidget {
  const ESPScaleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ESPScale',
      theme: ThemeData(
        colorSchemeSeed: Colors.teal,
        useMaterial3: true,
        brightness: Brightness.light,
      ),
      darkTheme: ThemeData(
        colorSchemeSeed: Colors.teal,
        useMaterial3: true,
        brightness: Brightness.dark,
      ),
      initialRoute: '/devices',
      onGenerateRoute: (settings) {
        Widget page;
        switch (settings.name) {
          case '/devices':
            page = const DeviceListScreen();
          case '/provision':
            page = const ProvisionScreen();
          case '/device':
            final deviceId = settings.arguments as String;
            page = DeviceDetailScreen(deviceId: deviceId);
          case '/calibrate':
            final deviceId = settings.arguments as String;
            page = CalibrationScreen(deviceId: deviceId);
          case '/device-settings':
            final deviceId = settings.arguments as String;
            page = DeviceSettingsScreen(deviceId: deviceId);
          case '/history':
            final deviceId = settings.arguments as String;
            page = HistoryScreen(deviceId: deviceId);
          case '/app-settings':
            page = const AppSettingsScreen();
          default:
            page = const DeviceListScreen();
        }
        return MaterialPageRoute(builder: (_) => page);
      },
    );
  }
}
