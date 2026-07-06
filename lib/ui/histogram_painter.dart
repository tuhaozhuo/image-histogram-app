// histogram_painter.dart — 绘制 256×100 黑白直方图（F5）。
//
// 逻辑分辨率固定 256×100：横轴 256 列对应灰度 0–255，每列高度为该灰度级
// 归一化值（0–100）。渲染时按显示区等比缩放，形态不变。
import 'package:flutter/material.dart';

class HistogramPainter extends CustomPainter {
  /// 长度 256，每项为归一化后的像素计数（0..100）。
  final List<int> normalized;

  const HistogramPainter(this.normalized);

  @override
  void paint(Canvas canvas, Size size) {
    // 白底。
    final Paint bg = Paint()..color = Colors.white;
    canvas.drawRect(Offset.zero & size, bg);

    if (normalized.length != 256) return;

    // 黑色竖条。
    final Paint bar = Paint()..color = Colors.black;
    final double colW = size.width / 256.0;
    for (int i = 0; i < 256; i++) {
      final double h = (normalized[i] / 100.0) * size.height;
      if (h <= 0) continue;
      // +1 宽度消除列间缝隙（相邻列相接）。
      canvas.drawRect(
        Rect.fromLTWH(i * colW, size.height - h, colW + 0.5, h),
        bar,
      );
    }
  }

  @override
  bool shouldRepaint(covariant HistogramPainter old) =>
      !identical(old.normalized, normalized);
}
