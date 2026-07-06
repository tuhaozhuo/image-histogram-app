// home_page.dart — 主界面：上传 → 计算 → 查看结果（F1/F6/F8 + 界面需求）。
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show rootBundle;
import 'package:image_picker/image_picker.dart';

import '../ffi/histogram_ffi.dart';
import 'histogram_painter.dart';

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final ImagePicker _picker = ImagePicker();

  XFile? _file;
  int _imgW = 0;
  int _imgH = 0;
  HistogramResult? _result;
  bool _processing = false;
  bool _synthetic = false; // 当前结果是否来自内置渐变测试图
  bool _assetSample = false; // 当前结果是否来自内置真实样张
  String? _error;

  static const String _samplePhotoAsset = 'assets/sample_photo.jpg';

  /// 解码图像字节为 RGBA 并调核心计算。返回 (结果, 宽, 高)。
  /// 解码不计入核心耗时（computeHistogram 只测 native 内部统计+归一化）。
  Future<(HistogramResult, int, int)> _decodeAndCompute(Uint8List bytes) async {
    final ui.Codec codec = await ui.instantiateImageCodec(bytes);
    final ui.FrameInfo frame = await codec.getNextFrame();
    final ui.Image image = frame.image;
    final int w = image.width;
    final int h = image.height;
    final ByteData? bd =
        await image.toByteData(format: ui.ImageByteFormat.rawRgba);
    image.dispose();
    if (bd == null) throw StateError('无法读取图像像素');
    final Uint8List rgba = bd.buffer.asUint8List();
    return (computeHistogram(rgba, w, h), w, h);
  }

  Future<void> _pickAndCompute(ImageSource source) async {
    try {
      final XFile? picked = await _picker.pickImage(source: source);
      if (picked == null) return; // 用户取消
      setState(() {
        _file = picked;
        _synthetic = false;
        _assetSample = false;
        _result = null;
        _error = null;
        _processing = true;
      });

      final Uint8List bytes = await picked.readAsBytes();
      final (HistogramResult result, int w, int h) =
          await _decodeAndCompute(bytes);

      if (!mounted) return;
      setState(() {
        _imgW = w;
        _imgH = h;
        _result = result;
        _processing = false;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _error = '$e';
        _processing = false;
      });
    }
  }

  /// 内置测试图：合成一张 3000×2000（6MP）渐变图，直接跑核心链路。
  /// 用于可复现的性能基准，不依赖相册/相机。生成数据的时间不计入核心耗时
  /// （computeHistogram 只测 native 内部的统计+归一化）。
  Future<void> _computeSynthetic() async {
    setState(() {
      _file = null;
      _synthetic = true;
      _assetSample = false;
      _result = null;
      _error = null;
      _processing = true;
    });
    // 让出一帧，先渲染 loading 再做重活。
    await Future<void>.delayed(const Duration(milliseconds: 16));
    try {
      const int w = 3000, h = 2000;
      final Uint8List rgba = Uint8List(w * h * 4);
      int idx = 0;
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          final int base = (x * 255) ~/ (w - 1);
          rgba[idx++] = base; // R
          rgba[idx++] = (base + y) & 0xFF; // G
          rgba[idx++] = 255 - base; // B
          rgba[idx++] = 255; // A
        }
      }
      final HistogramResult result = computeHistogram(rgba, w, h);
      if (!mounted) return;
      setState(() {
        _imgW = w;
        _imgH = h;
        _result = result;
        _processing = false;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _error = '$e';
        _processing = false;
      });
    }
  }

  /// 内置真实样张：打包的真实照片，一键解码+计算，展示真实照片的参差直方图
  /// （与渐变测试图的平顶梯形形成对比）。
  Future<void> _computeAssetSample() async {
    setState(() {
      _file = null;
      _synthetic = false;
      _assetSample = true;
      _result = null;
      _error = null;
      _processing = true;
    });
    await Future<void>.delayed(const Duration(milliseconds: 16));
    try {
      final ByteData data = await rootBundle.load(_samplePhotoAsset);
      final Uint8List bytes = data.buffer.asUint8List();
      final (HistogramResult result, int w, int h) =
          await _decodeAndCompute(bytes);
      if (!mounted) return;
      setState(() {
        _imgW = w;
        _imgH = h;
        _result = result;
        _processing = false;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _error = '$e';
        _processing = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final HistogramResult? r = _result;
    return Scaffold(
      appBar: AppBar(
        title: const Text('图像直方图 · 性能测试'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // ---- 原图预览区 ----
            _sectionTitle('原图预览'),
            _imagePreview(),
            const SizedBox(height: 16),

            // ---- 操作按钮 ----
            Row(
              children: [
                Expanded(
                  child: FilledButton.icon(
                    onPressed: _processing
                        ? null
                        : () => _pickAndCompute(ImageSource.gallery),
                    icon: const Icon(Icons.photo_library),
                    label: const Text('相册选择'),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: FilledButton.tonalIcon(
                    onPressed: _processing
                        ? null
                        : () => _pickAndCompute(ImageSource.camera),
                    icon: const Icon(Icons.photo_camera),
                    label: const Text('拍照'),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: _processing ? null : _computeSynthetic,
                    icon: const Icon(Icons.speed),
                    label: const Text('内置渐变图（基准）'),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: _processing ? null : _computeAssetSample,
                    icon: const Icon(Icons.landscape),
                    label: const Text('内置真实样张'),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 16),

            if (_processing)
              const Padding(
                padding: EdgeInsets.symmetric(vertical: 24),
                child: Center(child: CircularProgressIndicator()),
              ),

            if (_error != null)
              Card(
                color: Theme.of(context).colorScheme.errorContainer,
                child: Padding(
                  padding: const EdgeInsets.all(12),
                  child: Text('出错：$_error'),
                ),
              ),

            // ---- 直方图 + 耗时 ----
            if (r != null) ...[
              _sectionTitle('灰度直方图（256×100 黑白）'),
              AspectRatio(
                aspectRatio: 256 / 100,
                child: Container(
                  decoration: BoxDecoration(
                    border: Border.all(color: Colors.grey.shade400),
                    color: Colors.white,
                  ),
                  child: CustomPaint(
                    painter: HistogramPainter(r.normalized),
                    size: Size.infinite,
                  ),
                ),
              ),
              const SizedBox(height: 16),
              _metricsCard(r),
            ],
          ],
        ),
      ),
    );
  }

  Widget _sectionTitle(String text) => Padding(
        padding: const EdgeInsets.only(bottom: 8),
        child: Text(text,
            style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
      );

  Widget _imagePreview() {
    if (_assetSample) {
      return ClipRRect(
        borderRadius: BorderRadius.circular(8),
        child: Image.asset(
          _samplePhotoAsset,
          height: 180,
          width: double.infinity,
          fit: BoxFit.cover,
        ),
      );
    }
    if (_synthetic) {
      return Container(
        height: 180,
        decoration: BoxDecoration(
          color: Colors.indigo.shade50,
          borderRadius: BorderRadius.circular(8),
        ),
        child: const Center(
          child: Text('内置测试图（3000×2000 渐变）',
              style: TextStyle(color: Colors.indigo)),
        ),
      );
    }
    if (_file == null) {
      return Container(
        height: 180,
        decoration: BoxDecoration(
          color: Colors.grey.shade200,
          borderRadius: BorderRadius.circular(8),
        ),
        child: const Center(
          child: Text('尚未选择图像', style: TextStyle(color: Colors.grey)),
        ),
      );
    }
    return ClipRRect(
      borderRadius: BorderRadius.circular(8),
      child: Image.file(
        File(_file!.path),
        height: 180,
        width: double.infinity,
        fit: BoxFit.contain,
      ),
    );
  }

  Widget _metricsCard(HistogramResult r) {
    final bool pass = r.elapsedMs <= 300.0;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Text('核心耗时（统计+归一化）：',
                    style: TextStyle(fontSize: 15)),
                Text('${r.elapsedMs.toStringAsFixed(2)} ms',
                    style: TextStyle(
                      fontSize: 18,
                      fontWeight: FontWeight.bold,
                      color: pass ? Colors.green.shade700 : Colors.red,
                    )),
              ],
            ),
            const SizedBox(height: 6),
            Row(
              children: [
                Icon(pass ? Icons.check_circle : Icons.error,
                    color: pass ? Colors.green.shade700 : Colors.red, size: 18),
                const SizedBox(width: 6),
                Text(pass ? '达标（≤ 300ms）' : '超标（> 300ms）',
                    style: TextStyle(
                        color: pass ? Colors.green.shade700 : Colors.red)),
              ],
            ),
            const Divider(height: 20),
            Text('图像尺寸：$_imgW × $_imgH '
                '（${(_imgW * _imgH / 1e6).toStringAsFixed(1)} MP）'),
          ],
        ),
      ),
    );
  }
}
