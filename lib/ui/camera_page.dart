// camera_page.dart — 实时相机灰度直方图（创新点 2）。
//
// 相机预览流每帧取 YUV 的 Y 平面（Y = BT.601 亮度 = 标准灰度公式），
// 直接喂给 native `hist_compute_gray` 实时统计，验证「300ms 内」的实时意义。
import 'package:camera/camera.dart';
import 'package:flutter/material.dart';

import '../ffi/histogram_ffi.dart';
import 'histogram_painter.dart';

class CameraHistogramPage extends StatefulWidget {
  const CameraHistogramPage({super.key});

  @override
  State<CameraHistogramPage> createState() => _CameraHistogramPageState();
}

class _CameraHistogramPageState extends State<CameraHistogramPage> {
  CameraController? _controller;
  bool _busy = false; // 上一帧还在算就跳过，避免堆积
  List<int>? _hist;
  double _ms = 0;
  String? _error;

  @override
  void initState() {
    super.initState();
    _init();
  }

  Future<void> _init() async {
    try {
      final cameras = await availableCameras();
      if (cameras.isEmpty) throw StateError('无可用相机');
      final back = cameras.firstWhere(
        (c) => c.lensDirection == CameraLensDirection.back,
        orElse: () => cameras.first,
      );
      final ctrl = CameraController(
        back,
        ResolutionPreset.medium,
        enableAudio: false,
        imageFormatGroup: ImageFormatGroup.yuv420,
      );
      await ctrl.initialize();
      if (!mounted) {
        await ctrl.dispose();
        return;
      }
      _controller = ctrl;
      await ctrl.startImageStream(_onFrame);
      setState(() {});
    } catch (e) {
      if (mounted) setState(() => _error = '$e');
    }
  }

  void _onFrame(CameraImage image) {
    if (_busy || !mounted) return;
    _busy = true;
    try {
      final plane = image.planes[0]; // Y 平面 = 灰度
      final result = computeGrayHistogram(
          plane.bytes, image.width, image.height, plane.bytesPerRow);
      if (mounted) {
        setState(() {
          _hist = result.normalized;
          _ms = result.elapsedMs;
        });
      }
    } finally {
      _busy = false;
    }
  }

  @override
  void dispose() {
    _controller?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(title: const Text('实时相机直方图')),
      body: _error != null
          ? Center(
              child: Padding(
                padding: const EdgeInsets.all(24),
                child: Text('相机错误：$_error',
                    style: const TextStyle(color: Colors.white)),
              ),
            )
          : (_controller == null || !_controller!.value.isInitialized)
              ? const Center(child: CircularProgressIndicator())
              : Stack(
                  children: [
                    Center(child: CameraPreview(_controller!)),
                    Positioned(
                      left: 0,
                      right: 0,
                      bottom: 0,
                      child: Container(
                        color: Colors.black.withValues(alpha: 0.55),
                        padding: const EdgeInsets.all(12),
                        child: Column(
                          mainAxisSize: MainAxisSize.min,
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Row(
                              mainAxisAlignment: MainAxisAlignment.spaceBetween,
                              children: [
                                const Text('实时灰度直方图',
                                    style: TextStyle(
                                        color: Colors.white,
                                        fontWeight: FontWeight.bold)),
                                Text('核心 ${_ms.toStringAsFixed(2)} ms',
                                    style: TextStyle(
                                        color: Colors.greenAccent.shade100,
                                        fontWeight: FontWeight.bold)),
                              ],
                            ),
                            const SizedBox(height: 8),
                            AspectRatio(
                              aspectRatio: 256 / 100,
                              child: Container(
                                decoration:
                                    const BoxDecoration(color: Colors.white),
                                child: _hist == null
                                    ? null
                                    : CustomPaint(
                                        painter: HistogramPainter(_hist!),
                                        size: Size.infinite,
                                      ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
    );
  }
}
