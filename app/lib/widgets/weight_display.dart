import 'package:flutter/material.dart';

class WeightDisplay extends StatelessWidget {
  final double weight;
  final String unit;
  final bool stable;

  const WeightDisplay({
    super.key,
    required this.weight,
    this.unit = 'g',
    this.stable = true,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 24, horizontal: 16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Row(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.baseline,
              textBaseline: TextBaseline.alphabetic,
              children: [
                Text(weight.toStringAsFixed(weight < 10 ? 2 : 1),
                    style: theme.textTheme.displayLarge?.copyWith(
                      fontWeight: FontWeight.bold,
                      fontSize: 64,
                    )),
                const SizedBox(width: 8),
                Text(unit,
                    style: theme.textTheme.headlineMedium?.copyWith(
                      color: theme.colorScheme.onSurface.withValues(alpha: 0.6),
                    )),
              ],
            ),
            const SizedBox(height: 8),
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(
                  stable ? Icons.check_circle : Icons.sync,
                  size: 14,
                  color: stable ? Colors.green : Colors.orange,
                ),
                const SizedBox(width: 4),
                Text(stable ? 'Stable' : 'Measuring...',
                    style: theme.textTheme.bodySmall?.copyWith(
                      color: stable ? Colors.green : Colors.orange,
                    )),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
