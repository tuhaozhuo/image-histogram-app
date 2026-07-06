// 冒烟测试：应用能构建并显示主界面关键元素。
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:histogram_app/main.dart';

void main() {
  testWidgets('App builds and shows upload controls', (tester) async {
    await tester.pumpWidget(const HistogramApp());

    expect(find.text('图像直方图 · 性能测试'), findsOneWidget);
    expect(find.text('相册选择'), findsOneWidget);
    expect(find.text('尚未选择图像'), findsOneWidget);
  });
}
