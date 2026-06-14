import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

import '../models/weight_record.dart';

class WeightChart extends StatelessWidget {
  final List<WeightRecord> records;

  const WeightChart({super.key, required this.records});

  @override
  Widget build(BuildContext context) {
    if (records.isEmpty) {
      return const Center(child: Text('No data yet'));
    }

    final sorted = records.reversed.toList();
    final spots = sorted.asMap().entries.map((e) {
      return FlSpot(e.key.toDouble(), e.value.weight);
    }).toList();

    final minWeight = sorted.map((r) => r.weight).reduce((a, b) => a < b ? a : b);
    final maxWeight = sorted.map((r) => r.weight).reduce((a, b) => a > b ? a : b);
    final range = maxWeight - minWeight;
    final padding = range * 0.1;

    return LineChart(
      LineChartData(
        gridData: FlGridData(
          show: true,
          drawVerticalLine: false,
          horizontalInterval: range > 5 ? ((range + padding * 2) / 4) : 1,
        ),
        titlesData: FlTitlesData(
          leftTitles: AxisTitles(sideTitles: SideTitles(
            showTitles: true,
            reservedSize: 40,
            getTitlesWidget: (v, _) => Text(v.toStringAsFixed(1), style: const TextStyle(fontSize: 10)),
          )),
          bottomTitles: AxisTitles(sideTitles: SideTitles(showTitles: false)),
          topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
          rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
        ),
        borderData: FlBorderData(show: false),
        minY: minWeight - padding,
        maxY: maxWeight + padding,
        lineBarsData: [
          LineChartBarData(
            spots: spots,
            isCurved: true,
            color: Theme.of(context).colorScheme.primary,
            barWidth: 2,
            isStrokeCapRound: true,
            dotData: FlDotData(show: false),
            belowBarData: BarAreaData(
              show: true,
              color: Theme.of(context).colorScheme.primary.withOpacity(0.1),
            ),
          ),
        ],
        lineTouchData: LineTouchData(
          touchTooltipData: LineTouchTooltipData(
            getTooltipItems: (touched) => touched.map((t) =>
                LineTooltipItem('${t.y.toStringAsFixed(1)} g', const TextStyle(color: Colors.white))).toList(),
          ),
        ),
      ),
    );
  }
}
