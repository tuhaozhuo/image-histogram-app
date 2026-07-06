# CLAUDE.md — 图像直方图双端 App

课题「图像直方图计算及性能优化」。用户上传图片 → 高性能计算灰度直方图（≤300ms）→ 显示 256×100 黑白直方图 + 生成耗时。评分核心两点：**计算速度（≤300ms）** 与 **结果准确性**。

## 架构

Flutter（UI 单代码库双端）+ 共享 C++ 计算核心（dart:ffi）。计算核心是第一等公民，双端复用同一份 `native/histogram.cpp`。

## 目录结构约定

```
histogram_app/
├── CLAUDE.md              # 本文件，规则先行
├── native/               # 平台无关 C++ 核心（Android NDK / iOS / macOS bench 共用）
│   ├── histogram.h       # extern "C" ABI
│   ├── histogram.cpp     # 核心算法
│   ├── third_party/      # stb_image.h（仅 bench 解码真实图片用）
│   ├── bench/bench.cpp   # macOS 原生基准 + 正确性对拍
│   └── CMakeLists.txt
├── lib/                  # Flutter Dart 代码（toolchain 就绪后）
└── pubspec.yaml
```

约定：C++ 全部放 `native/`，不因平台复制多份。Dart 侧 FFI 绑定集中在 `lib/ffi/`，UI 在 `lib/ui/`。临时构建产物放 `native/build/`（git 忽略），不入库。

## 计时口径（强制统一）

耗时 = **从统计开始到归一化完成**（生成可绘制的 256 数组）。
**不含**图像解码、内存拷贝、UI 绘制。核心函数 `hist_compute_rgba` 内部用 `std::chrono::steady_clock` 只包住「统计 + 归一化」两段，返回毫秒。对齐需求文档第 42 行与风险表末行。

## 精度口径（强制统一）

灰度化公式：`gray = R×0.299 + G×0.587 + B×0.114`，四舍五入取整落入 0–255。
定点实现：`gray = (R*19595 + G*38470 + B*7471 + 32768) >> 16`
（19595+38470+7471 = 65536，即三系数的 Q16 定点；+32768 实现四舍五入）。
必须与 `round(R*0.299+G*0.587+B*0.114)` 的 double 参考实现**逐 bin 计数完全一致**，由 bench 对拍保证。

归一化：`out[i] = round(count[i] / max(count) × 100)`，落入 0–100。**特判 max==0**（纯黑图）→ 全 0，不除零。

## 优化手段

统计时灰度化（一次遍历）+ 定点整数 + 分块多线程（按行切，各线程本地 256 直方图，最后归并，无锁）+ 按行连续访问。必要时 NEON。

## 验证命令

```
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build
./native/build/bench
```
判定标准：12MP 图耗时 ≤300ms，且对拍全部 PASS。改完核心必须跑此验证，不只改不验。

## 构建环境与运行（Flutter / Android）

工具链：Flutter 3.44.4（brew cask）、Android Studio（其自带 JBR 提供 Java）、
Android SDK 命令行工具（brew cask android-commandlinetools）。

**必需环境变量**（每次构建前 export）：
```
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
export ANDROID_SDK_ROOT=/opt/homebrew/share/android-commandlinetools
export ANDROID_HOME=$ANDROID_SDK_ROOT
export PATH="$JAVA_HOME/bin:$ANDROID_SDK_ROOT/platform-tools:$ANDROID_SDK_ROOT/emulator:/opt/homebrew/bin:$PATH"
```

**已知坑**：brew 链接的 `sdkmanager`/`avdmanager` wrapper 有 bug（`test: integer
expression expected` 会导致多包安装中途退出且残留不完整目录）。装 SDK 组件请直接用
真实二进制：`$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager --sdk_root=$ANDROID_SDK_ROOT ...`。
NDK 若报缺 `source.properties`，说明上次装残缺，`--uninstall` 后重装。

已装 SDK：platform-tools、platforms;android-35/36、build-tools;35/36、
ndk;27.0.12077973、cmake;3.22.1、emulator、system-images;android-35;google_apis;arm64-v8a。
NDK 待办：插件要求 28.2.13676358（当前 27，向后兼容仅 warning），有需要再对齐。

**构建 / 运行**：
```
flutter build apk --debug                 # 产物含 libhistogram.so（NDK+CMake 编译 native/）
adb install -r build/app/outputs/flutter-apk/app-debug.apk
emulator -avd hist_pixel                   # arm64 模拟器（与真机同架构）
```
Android 侧通过 android/app/build.gradle.kts 的 externalNativeBuild 复用 native/CMakeLists.txt。

## 构建环境与运行（iOS）

需完整 Xcode（非 Command Line Tools）+ CocoaPods。首次准备：
```
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -runFirstLaunch && sudo xcodebuild -license accept
brew install cocoapods
```

C++ 核心通过 **CocoaPods 本地 pod** 接入：`native/histogram_core.podspec` 编译
`histogram.cpp`（同样 `-ffp-contract=off -O3`），在 `ios/Podfile` 中
`pod 'histogram_core', :path => '../native'`。Dart 侧用 `DynamicLibrary.process()`
查找符号。

**关键点/坑**：
- `histogram.h` 的导出函数带 `HIST_EXPORT`（`used + visibility(default)`），
  否则 iOS 静态链接时无 Obj-C 引用会被 dead-strip；podspec 里
  `GCC_SYMBOLS_PRIVATE_EXTERN=NO` 双保险。
- 首次 `pod install` 后，需在 `ios/Flutter/Debug.xcconfig`、`Release.xcconfig`
  顶部 `#include?` 对应的 `Pods-Runner.*.xcconfig`，否则 Pods 设置不生效、编译失败。
- `ios/Runner/Info.plist` 已加 `NSPhotoLibraryUsageDescription` /
  `NSCameraUsageDescription`（image_picker 必需）。

```
cd ios && pod install && cd ..
flutter build ios --simulator --debug
xcrun simctl install booted build/ios/iphonesimulator/Runner.app
xcrun simctl launch booted com.histogramapp.histogramApp
```

## 边界与容错

纯黑图（max==0）、超大图、异常尺寸不崩溃。max==0 特判。多线程归并需覆盖行数不能整除线程数的余数。
