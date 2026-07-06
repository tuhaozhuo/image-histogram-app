# 图像直方图 · 双端 App

课题「图像直方图计算及性能优化」的实现：用户上传图片 → 高性能计算灰度直方图（≤300ms）→ 显示 256×100 黑白直方图 + 生成耗时。

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
| Android 模拟器(debug) | 6MP 真实照片 | ~8 ms | bin-exact |

均远低于 300ms 预算。

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

## 状态

- [x] Android 端完整跑通（相册/拍照/内置图，直方图 + 耗时，真机验证）
- [ ] iOS 端接线（podspec + 符号保留，复用同一 C++ 核心）
