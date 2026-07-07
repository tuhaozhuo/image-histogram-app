// histogram_ffi.dart — dart:ffi 绑定，调用共享 C++ 计算核心。
//
// 对应 native/histogram.h 的 C ABI：
//   double hist_compute_rgba(const uint8_t*, int32, int32, int32*)
import 'dart:ffi';
import 'dart:io' show Platform;
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

// C 侧函数签名。
typedef _HistComputeNative = Double Function(
    Pointer<Uint8> rgba, Int32 width, Int32 height, Pointer<Int32> out256);
typedef _HistComputeDart = double Function(
    Pointer<Uint8> rgba, int width, int height, Pointer<Int32> out256);

typedef _HistImplNative = Double Function(Pointer<Uint8> rgba, Int32 width,
    Int32 height, Pointer<Int32> out256, Int32 impl);
typedef _HistImplDart = double Function(
    Pointer<Uint8> rgba, int width, int height, Pointer<Int32> out256, int impl);

typedef _HistGrayNative = Double Function(Pointer<Uint8> gray, Int32 width,
    Int32 height, Int32 rowStride, Pointer<Int32> out256);
typedef _HistGrayDart = double Function(
    Pointer<Uint8> gray, int width, int height, int rowStride,
    Pointer<Int32> out256);

// 实现常量（对应 histogram.h 的 HIST_IMPL_*）。
const int kImplScalarST = 0;
const int kImplScalarMT = 1;
const int kImplNeonMT = 2;

DynamicLibrary _openLibrary() {
  if (Platform.isAndroid) {
    // NDK externalNativeBuild 产物，打包在 APK 内。
    return DynamicLibrary.open('libhistogram.so');
  }
  if (Platform.isIOS) {
    // iOS 静态链入进程，符号在主可执行文件中（podspec 接线 + 符号保留）。
    return DynamicLibrary.process();
  }
  // 桌面调试（macOS 等）：加载 bench 阶段构建的动态库。
  if (Platform.isMacOS) return DynamicLibrary.open('libhistogram.dylib');
  return DynamicLibrary.open('libhistogram.so');
}

final DynamicLibrary _lib = _openLibrary();
final _HistComputeDart _histComputeRgba =
    _lib.lookupFunction<_HistComputeNative, _HistComputeDart>(
        'hist_compute_rgba');
final _HistImplDart _histComputeImpl =
    _lib.lookupFunction<_HistImplNative, _HistImplDart>(
        'hist_compute_rgba_impl');
final _HistGrayDart _histComputeGray =
    _lib.lookupFunction<_HistGrayNative, _HistGrayDart>('hist_compute_gray');

/// 直方图计算结果：256 个归一化值（0..100）+ 核心耗时（毫秒，仅统计+归一化）。
class HistogramResult {
  final List<int> normalized; // 长度 256
  final double elapsedMs;
  const HistogramResult(this.normalized, this.elapsedMs);
}

/// 对 RGBA8888 像素缓冲计算灰度直方图。
///
/// [rgba] 长度必须为 width*height*4。返回的 elapsedMs 是 C 核心内部实测的
/// 「统计+归一化」耗时，不含此处的内存拷贝（对齐评分计时口径）。
HistogramResult computeHistogram(Uint8List rgba, int width, int height) {
  final Pointer<Uint8> rgbaPtr = malloc<Uint8>(rgba.length);
  final Pointer<Int32> outPtr = malloc<Int32>(256);
  try {
    // 拷贝像素到 native 堆（拷贝耗时不计入核心计时）。
    rgbaPtr.asTypedList(rgba.length).setAll(0, rgba);
    final double ms = _histComputeRgba(rgbaPtr, width, height, outPtr);
    final List<int> out =
        List<int>.generate(256, (i) => outPtr[i], growable: false);
    return HistogramResult(out, ms);
  } finally {
    malloc.free(rgbaPtr);
    malloc.free(outPtr);
  }
}

/// 对灰度平面（相机 YUV 的 Y 通道）直接计算直方图。
/// [gray] 长度 = rowStride*height（含行对齐 padding），[rowStride] 每行字节数。
/// 用于实时相机——Y 通道即 BT.601 亮度，无需 RGB 转换。
HistogramResult computeGrayHistogram(
    Uint8List gray, int width, int height, int rowStride) {
  final Pointer<Uint8> grayPtr = malloc<Uint8>(gray.length);
  final Pointer<Int32> outPtr = malloc<Int32>(256);
  try {
    grayPtr.asTypedList(gray.length).setAll(0, gray);
    final double ms =
        _histComputeGray(grayPtr, width, height, rowStride, outPtr);
    final List<int> out =
        List<int>.generate(256, (i) => outPtr[i], growable: false);
    return HistogramResult(out, ms);
  } finally {
    malloc.free(grayPtr);
    malloc.free(outPtr);
  }
}

/// 单个实现的对比结果。
class ImplResult {
  final String name;
  final double elapsedMs;
  final double speedup; // 相对单线程 baseline
  final int mismatchBins; // 与默认精确实现的归一化偏差 bin 数
  const ImplResult(this.name, this.elapsedMs, this.speedup, this.mismatchBins);
}

/// 对同一图跑三种实现（单线程/多线程/NEON），返回耗时对比。
/// 每种取多次运行的最小耗时；精度以默认精确实现（多线程标量）为基准比对。
List<ImplResult> compareImplementations(Uint8List rgba, int width, int height) {
  final Pointer<Uint8> rgbaPtr = malloc<Uint8>(rgba.length);
  final Pointer<Int32> outPtr = malloc<Int32>(256);
  try {
    rgbaPtr.asTypedList(rgba.length).setAll(0, rgba);
    const impls = [kImplScalarST, kImplScalarMT, kImplNeonMT];
    const names = ['单线程标量', '多线程标量', 'NEON 多线程'];

    // 先用精确实现算基准直方图，供精度比对。
    _histComputeImpl(rgbaPtr, width, height, outPtr, kImplScalarMT);
    final List<int> ref =
        List<int>.generate(256, (i) => outPtr[i], growable: false);

    double base = 0;
    final results = <ImplResult>[];
    for (int i = 0; i < impls.length; i++) {
      _histComputeImpl(rgbaPtr, width, height, outPtr, impls[i]); // 预热
      double best = double.infinity;
      List<int> out = ref;
      for (int r = 0; r < 5; r++) {
        final ms = _histComputeImpl(rgbaPtr, width, height, outPtr, impls[i]);
        if (ms < best) {
          best = ms;
          out = List<int>.generate(256, (j) => outPtr[j], growable: false);
        }
      }
      if (i == 0) base = best;
      int mism = 0;
      for (int j = 0; j < 256; j++) {
        if (out[j] != ref[j]) mism++;
      }
      results.add(ImplResult(names[i], best, base / best, mism));
    }
    return results;
  } finally {
    malloc.free(rgbaPtr);
    malloc.free(outPtr);
  }
}
