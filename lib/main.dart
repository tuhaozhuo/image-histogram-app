// main.dart — 应用入口。
import 'package:flutter/material.dart';

import 'ui/home_page.dart';

void main() {
  runApp(const HistogramApp());
}

class HistogramApp extends StatelessWidget {
  const HistogramApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: '图像直方图',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.indigo),
        useMaterial3: true,
      ),
      home: const HomePage(),
    );
  }
}
