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
