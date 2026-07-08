# 图像直方图 · 双端 App

课题「图像直方图计算及性能优化」的实现：用户上传图片 → 高性能计算灰度直方图（≤300ms）→ 显示 256×100 黑白直方图 + 生成耗时。

发布：[GitHub Releases](https://github.com/tuhaozhuo/image-histogram-app/releases)（v1.2.0，含可安装 Android APK）。

📖 **完整开发手册**（分层讲解 / 三实现原理与精度 / 术语数字词典 / 快速上手 / **微信小程序移植指南** / 准确性验证）见 [docs/开发手册.md](docs/开发手册.md)。
🧪 **测试指南**（面向测试人员：功能/性能/准确性/边界/双端用例）见 [docs/测试指南.md](docs/测试指南.md)。
✅ **准确性测试报告**（穷举 + 对拍实测结果记录）见 [docs/准确性测试报告.md](docs/准确性测试报告.md)。

## 架构

**Flutter（UI 单代码库，Android/iOS 双端）+ 共享 C++ 计算核心（dart:ffi）**。
计算核心是第一等公民，双端复用同一份 `native/histogram.cpp`。

```
histogram_app/
├── native/                # 平台无关 C++ 核心（Android NDK / iOS / macOS bench 共用）
│   ├── histogram.h/.cpp   # 核心算法：统计时灰度化 + 分块多线程 + 局部直方图归并
│   ├── bench/bench.cpp    # macOS 原生基准 + 与标准公式逐 bin 对拍 + ASCII 直方图
│   └── CMakeLists.txt      # bench 与 Android NDK externalNativeBuild 双用途
├── lib/
│   ├── ffi/histogram_ffi.dart   # dart:ffi 绑定，加载 libhistogram.so
│   └── ui/                       # 选图/解码/调核心/绘制/耗时显示
└── assets/sample_photo.jpg      # 内置真实样张
```

## 算法要点

- **准确性**：严格采用标准公式 `gray = round(R*0.299 + G*0.587 + B*0.114)`，
  直写表达式 + `-ffp-contract=off`（禁 FMA 融合），与标准公式逐 bin 完全一致，
  跨平台结果确定。
- **性能**：统计时灰度化（一次遍历）+ 按行分块多线程 + 每线程本地 256 直方图无锁归并。
- **计时口径**：只测「统计 + 归一化」，不含解码/绘制（对齐评分口径）。
- **归一化**：`out[i] = round(count[i] / max(count) × 100)`，max==0 特判防除零。

## 性能实测

| 环境 | 图像 | 耗时 | 准确性 |
|---|---|---|---|
| macOS arm64 bench | 12MP 合成图 | ~4 ms | 与标准公式 bin-exact |
| **iOS 真机 (iPhone 17, release)** | 6MP | **~5 ms（均 <10ms）** | bin-exact |
| Android 真机 (realme, release) | 6MP | 已验证达标 | bin-exact |
| Android 模拟器(debug) | 6MP 真实照片 | ~8 ms | bin-exact |
| iOS 模拟器(debug) | 6MP 渐变图 | ~20 ms | bin-exact |

均远低于 300ms 预算（模拟器/debug 有虚拟化开销，真机 release 最快）。

## 创新功能（性能优化深化）

### 三实现性能对比（标量 / 多线程 / NEON）

app 内一键对内置渐变图（3000×2000）跑三种实现，展示优化幅度（macOS bench 12MP）：

| 实现 | 耗时 | 加速比 | 精度 |
|---|---|---|---|
| 单线程标量（baseline） | ~10 ms | 1.0× | 精确 |
| 多线程标量 | ~3 ms | ~3–4× | 精确 |
| NEON 多线程 | ~2.7 ms | ~3.9× | 5 bins ±1 |

NEON 用 `float32` 向量化灰度化，个别 bin 有 ±1 舍入差（性能/精度权衡，如实标注）。
观察：NEON 提升有限——直方图累加是 scatter（每像素散射到不同 bin）无法向量化，
真正瓶颈在累加而非灰度化。

### 实时相机直方图

相机预览流每帧实时计算直方图，验证「300ms 内」的实时意义。关键点：相机 YUV 的
**Y 通道即 BT.601 亮度 `Y=0.299R+0.587G+0.114B`——正好是标准灰度公式**，直接统计
（`hist_compute_gray`），无需 RGB 转换。iOS 真机实时流畅。

## 构建与运行

**C++ 核心基准（可立即验证）**
```bash
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build
./native/build/bench            # 打印耗时 + 对拍 + ASCII 直方图
```

**Android**（构建环境与踩坑见 [CLAUDE.md](CLAUDE.md)）
```bash
flutter pub get
flutter build apk --release
```

**iOS**（需完整 Xcode + CocoaPods；C++ 通过 `native/histogram_core.podspec` 编译）
```bash
cd ios && pod install && cd ..
flutter build ios --simulator --debug        # 或真机 --release
```

## 状态

- [x] Android 端完整跑通（相册/拍照/内置图，直方图 + 耗时，真机验证）
- [x] iOS 端接线完成（CocoaPods 本地 pod + `DynamicLibrary.process()`，复用同一 C++ 核心，模拟器 + 真机验证 ~5ms）
- [x] 创新①：NEON SIMD 向量化 + 三实现性能对比（bench + app 内可视化，双端真机验证）
- [x] 创新②：实时相机直方图（YUV Y 通道直接统计，iOS 真机实时流畅）
