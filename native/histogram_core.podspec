# histogram_core.podspec — 让 iOS 通过 CocoaPods 编译共享 C++ 计算核心。
#
# 在 ios/Podfile 的 target 'Runner' 中引用：
#   pod 'histogram_core', :path => '../native'
# 编译进 Runner 后，Dart 侧用 DynamicLibrary.process() 查找 hist_compute_rgba。
Pod::Spec.new do |s|
  s.name             = 'histogram_core'
  s.version          = '1.0.0'
  s.summary          = 'Shared C++ grayscale histogram core (dart:ffi).'
  s.description      = '平台无关的灰度直方图计算核心，iOS 通过 dart:ffi 调用。'
  s.homepage         = 'https://github.com/tuhaozhuo/image-histogram-app'
  s.license          = { :type => 'MIT' }
  s.author           = { 'tuhaozhuo' => 'tutuzhuo.31844@outlook.com' }
  s.source           = { :path => '.' }

  s.source_files        = 'histogram.cpp', 'histogram.h'
  s.public_header_files = 'histogram.h'
  s.requires_arc        = false
  s.library             = 'c++'
  s.ios.deployment_target = '12.0'

  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CLANG_CXX_LIBRARY'           => 'libc++',
    # 与 Android/macOS 一致：-O3 + 禁 FMA 融合，保证灰度结果跨平台确定。
    'OTHER_CPLUSPLUS_FLAGS'       => '-ffp-contract=off -O3',
    # 不隐藏符号，配合 histogram.h 的 used 属性，保证 FFI 能查找到导出函数。
    'GCC_SYMBOLS_PRIVATE_EXTERN'  => 'NO',
  }
end
