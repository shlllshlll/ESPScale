import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:esp_scale_app/main.dart';

void main() {
  testWidgets('App launches smoke test', (WidgetTester tester) async {
    await tester.pumpWidget(
      const ProviderScope(child: ESPScaleApp()),
    );
    expect(find.byType(MaterialApp), findsOneWidget);
  });
}
