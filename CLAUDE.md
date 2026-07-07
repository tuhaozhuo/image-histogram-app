# CLAUDE.md — 图像直方图双端 App

课题「图像直方图计算及性能优化」。用户上传图片 → 高性能计算灰度直方图（≤300ms）→ 显示 256×100 黑白直方图 + 生成耗时。评分核心两点：**计算速度（≤300ms）** 与 **结果准确性**。

> 面向人类开发者/协作者的完整讲解（代码逐层、三实现原理与精度、术语词典、小程序移植）见 `docs/开发手册.md`。本文件是给 AI/开发者的规则手册（约定、踩坑、构建环境）。

## 架构

Flutter（UI 单代码库双端）+ 共享 C++ 计算核心（dart:ffi）。计算核心是第一等公民，双端复用同一份 `native/histogram.cpp`。

## 目录结构约定

```
histogram_app/
├── CLAUDE.md · README.md
├── native/               # 平台无关 C++ 核心（Android NDK / iOS / macOS bench 共用）
│   ├── histogram.{h,cpp}       # C ABI + 核心算法
│   ├── histogram_core.podspec  # iOS CocoaPods 本地 pod（编译 histogram.cpp）
│   ├── third_party/            # stb_image.h（仅 bench 解码真实图片用）
│   ├── bench/bench.cpp         # macOS 原生基准 + 对拍 + ASCII 直方图
│   └── CMakeLists.txt          # bench 与 Android NDK externalNativeBuild 双用途
├── lib/
│   ├── ffi/              # dart:ffi 绑定（加载/查找 hist_compute_rgba）
│   ├── ui/              # 选图/解码/调核心/绘制/耗时显示
│   └── main.dart
├── assets/sample_photo.jpg     # 内置真实样张（自拍荷花，非 Apple 版权素材）
├── android/ · ios/             # 各自接入同一 native/ 核心（见下方构建节）
└── pubspec.yaml
```

约定：C++ 全部放 `native/`，不因平台复制多份。Dart 侧 FFI 绑定集中在 `lib/ffi/`，UI 在 `lib/ui/`。临时构建产物放 `native/build/`（git 忽略），不入库。

## 计时口径（强制统一）

耗时 = **从统计开始到归一化完成**（生成可绘制的 256 数组）。
**不含**图像解码、内存拷贝、UI 绘制。核心函数 `hist_compute_rgba` 内部用 `std::chrono::steady_clock` 只包住「统计 + 归一化」两段，返回毫秒。对齐需求文档第 42 行与风险表末行。

## 精度口径（强制统一）

灰度化公式：`gray = round(R×0.299 + G×0.587 + B×0.114)`，取整落入 0–255。
实现直写该 double 表达式并 `std::lround`，配合编译期 **`-ffp-contract=off`**（禁 FMA 融合），
保证与标准公式**逐 bin 计数完全一致**、且 Android/iOS/macOS 跨平台结果确定。由 bench 对拍保证。

> 曾试 Q16 定点近似（`(R*19595+G*38470+B*7471+32768)>>16`），因半整数 tie 点无法与
> 标准公式 bit 对齐（网罗 256³ 组合实测仍有数千处 ±1 偏差）而**放弃**；速度余量充足
> （12MP ~4ms，预算 300ms），无需为性能牺牲准确性。不要再改回定点。

归一化：`out[i] = round(count[i] / max(count) × 100)`，落入 0–100。**特判 max==0** → 全 0，不除零。

## 优化手段与多实现

统计时灰度化（一次遍历）+ 分块多线程（按行切，各线程本地 256 直方图，最后归并，无锁）+ 按行连续访问。

三种实现供性能对比（`histogram.h` 的 `HIST_IMPL_*`）：单线程标量 / 多线程标量 / NEON 多线程。
**默认 `hist_compute_rgba` 用多线程标量 double，保证 bin-exact**；NEON（`float32` 向量化）
最快但个别 bin ±1，**仅用于对比展示，不要设为默认**（会牺牲准确性）。注意：NEON 只向量化
灰度化，直方图累加是 scatter 无法向量化，提升有限。

实时相机走 `hist_compute_gray`：相机 YUV 的 Y 通道即 BT.601 亮度 = 标准灰度公式，直接统计，
**不要加 RGB 转换**（Y 已是灰度，转换是错的）。相机权限：Android `CAMERA`（AndroidManifest）、
iOS `NSCameraUsageDescription`（Info.plist），均已加。

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
NDK 版本：插件建议 28.2.13676358，当前用 27（向后兼容，仅一条无害 warning）。
**已决定保留 27 不对齐**——功能/性能无差异，不值得为消 warning 下 1GB NDK。

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

**`pod install` 必须设 `LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8`**，否则中文路径「短学期」
触发 CocoaPods 的 Ruby 编码错误（Encoding::CompatibilityError）而崩溃。

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

模拟器：
```
cd ios && pod install && cd ..
flutter build ios --simulator --debug
xcrun simctl install booted build/ios/iphonesimulator/Runner.app
xcrun simctl launch booted com.histogramapp.histogramApp
```

**真机部署坑**：
- iPhone 需先开「开发者模式」（设置 → 隐私与安全性 → 开发者模式 → 开 → 重启）。
- 签名用 automatic + 免费 team（`DEVELOPMENT_TEAM=X92HCRYMBL`）。每加一台新设备需
  Apple ID **在线**登录重新生成 provisioning profile；命令行报 provisioning/登录失败时，
  用 `open ios/Runner.xcworkspace` 在 Xcode 里配 Signing 并直接 Run 最省心。
- **真机验证必须用 Release，不能用 Debug**：Debug 版是 JIT，脱离电脑独立启动会闪退
  （日志 `Dart execution mode: JIT`）；Release 是 AOT，独立运行正常。
  `flutter build ios --release` →
  `xcrun devicectl device install app --device <id> build/ios/iphoneos/Runner.app`。

## 边界与容错

纯黑图（max==0）、超大图、异常尺寸不崩溃。max==0 特判。多线程归并需覆盖行数不能整除线程数的余数。
