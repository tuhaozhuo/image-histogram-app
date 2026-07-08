// histogram.cpp — 灰度直方图核心算法（多实现，供性能对比）
//
// 三种实现（见 histogram.h 的 impl 常量）：
//   0 SCALAR_ST  单线程标量 double —— baseline
//   1 SCALAR_MT  多线程标量 double —— 默认，与标准公式 bin-exact
//   2 NEON_MT    多线程 + NEON 向量化灰度化 —— 最快，float32 舍入有微小精度权衡
//
// 精度：标量版严格标准公式 round(R*.299+G*.587+B*.114) + std::lround，配合
//   -ffp-contract=off 保证 bin-exact（详见 CLAUDE.md）。NEON 版用 float32 +
//   round-to-nearest，在半整数 tie 点与标准公式可能 ±1，属性能/精度权衡。
#include "histogram.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace {

// 标准灰度公式：gray = round(R*0.299 + G*0.587 + B*0.114)。
inline int gray_of(uint8_t r, uint8_t g, uint8_t b) {
  const double gray = r * 0.299 + g * 0.587 + b * 0.114;
  int gi = static_cast<int>(std::lround(gray));
  if (gi > 255) gi = 255;
  return gi;
}

// 标量：对 [row_begin, row_end) 行区间统计局部直方图。
void accumulate_scalar(const uint8_t* rgba, int32_t width, int32_t row_begin,
                       int32_t row_end, uint32_t* hist) {
  std::memset(hist, 0, 256 * sizeof(uint32_t));
  const size_t row_stride = static_cast<size_t>(width) * 4;
  for (int32_t y = row_begin; y < row_end; ++y) {
    const uint8_t* p = rgba + static_cast<size_t>(y) * row_stride;
    for (int32_t x = 0; x < width; ++x) {
      ++hist[gray_of(p[0], p[1], p[2])];
      p += 4;
    }
  }
}

#if defined(__ARM_NEON)
// NEON：向量化灰度化（一次 8 像素），直方图累加仍标量（scatter 无法向量化）。
void accumulate_neon(const uint8_t* rgba, int32_t width, int32_t row_begin,
                     int32_t row_end, uint32_t* hist) {
  std::memset(hist, 0, 256 * sizeof(uint32_t));
  const float32x4_t wr = vdupq_n_f32(0.299f);
  const float32x4_t wg = vdupq_n_f32(0.587f);
  const float32x4_t wb = vdupq_n_f32(0.114f);
  const float32x4_t half = vdupq_n_f32(0.5f);  // +0.5 后截断 = 四舍五入（正数）
  const size_t row_stride = static_cast<size_t>(width) * 4;
  for (int32_t y = row_begin; y < row_end; ++y) {
    const uint8_t* p = rgba + static_cast<size_t>(y) * row_stride;
    int32_t x = 0;
    for (; x + 8 <= width; x += 8) {
      const uint8x8x4_t px = vld4_u8(p);  // 分离 R/G/B/A，各 8 个
      const uint16x8_t r16 = vmovl_u8(px.val[0]);
      const uint16x8_t g16 = vmovl_u8(px.val[1]);
      const uint16x8_t b16 = vmovl_u8(px.val[2]);
      // 低 4 像素
      float32x4_t ylo = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(r16))), wr);
      ylo = vaddq_f32(ylo, vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(g16))), wg));
      ylo = vaddq_f32(ylo, vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(b16))), wb));
      // 高 4 像素
      float32x4_t yhi = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(r16))), wr);
      yhi = vaddq_f32(yhi, vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(g16))), wg));
      yhi = vaddq_f32(yhi, vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(b16))), wb));
      // 四舍五入 → int（gray>=0，+0.5 截断）
      int32_t tmp[8];
      vst1q_s32(tmp, vcvtq_s32_f32(vaddq_f32(ylo, half)));
      vst1q_s32(tmp + 4, vcvtq_s32_f32(vaddq_f32(yhi, half)));
      for (int k = 0; k < 8; ++k) {
        int gi = tmp[k];
        if (gi > 255) gi = 255;
        ++hist[gi];
      }
      p += 32;
    }
    for (; x < width; ++x) {  // 尾部不足 8 的像素走标量
      ++hist[gray_of(p[0], p[1], p[2])];
      p += 4;
    }
  }
}
#endif  // __ARM_NEON

// 分块多线程 + 局部直方图无锁归并；use_neon 选累加实现。
void accumulate_parallel(const uint8_t* rgba, int32_t width, int32_t height,
                         uint32_t* hist, bool use_neon) {
  auto fn = accumulate_scalar;
#if defined(__ARM_NEON)
  if (use_neon) fn = accumulate_neon;
#else
  (void)use_neon;
#endif

  unsigned hw = std::thread::hardware_concurrency();
  if (hw == 0) hw = 4;
  int32_t num_threads = static_cast<int32_t>(hw);
  const int32_t min_rows = 64;
  if (height < num_threads * min_rows) {
    num_threads = std::max(1, height / min_rows);
  }
  if (num_threads < 1) num_threads = 1;

  if (num_threads == 1) {
    fn(rgba, width, 0, height, hist);
    return;
  }
  std::vector<std::thread> threads;
  threads.reserve(num_threads - 1);
  // 关键设计：每线程一份独立的局部直方图，各写各的 → 全程无锁、无原子操作、
  // 无数据竞争；所有线程结束后再由主线程整数相加归并（下方）。若共享一个直方图，
  // hist[gray]++ 会竞争，必须加锁，反而拖慢。
  std::vector<std::vector<uint32_t>> locals(num_threads,
                                            std::vector<uint32_t>(256));
  const int32_t base = height / num_threads;
  const int32_t rem = height % num_threads;
  int32_t row = 0;
  for (int32_t t = 0; t < num_threads; ++t) {
    const int32_t rows = base + (t < rem ? 1 : 0);
    const int32_t begin = row;
    const int32_t end = row + rows;
    row = end;
    if (t == num_threads - 1) {
      fn(rgba, width, begin, end, locals[t].data());
    } else {
      threads.emplace_back(fn, rgba, width, begin, end, locals[t].data());
    }
  }
  for (auto& th : threads) th.join();
  for (int32_t t = 0; t < num_threads; ++t) {
    const uint32_t* l = locals[t].data();
    for (int i = 0; i < 256; ++i) hist[i] += l[i];
  }
}

// 统一入口：impl 选实现，返回「统计 + 归一化」耗时（毫秒）。
double compute(const uint8_t* rgba, int32_t width, int32_t height,
               int32_t* out256, int32_t impl) {
  if (out256 != nullptr) std::memset(out256, 0, 256 * sizeof(int32_t));
  if (rgba == nullptr || out256 == nullptr || width <= 0 || height <= 0) {
    return 0.0;
  }

  // 计时口径（评分核心）：t0…t1 严格只包住「统计 + 归一化」两段，
  // 不含图像解码、输入拷贝、UI 绘制——测的是纯算法性能。
  const auto t0 = std::chrono::steady_clock::now();

  uint32_t hist[256];
  std::memset(hist, 0, sizeof(hist));
  if (impl == HIST_IMPL_SCALAR_ST) {
    accumulate_scalar(rgba, width, 0, height, hist);  // 单线程 baseline
  } else {
    accumulate_parallel(rgba, width, height, hist, impl == HIST_IMPL_NEON_MT);
  }

  uint32_t maxv = 0;
  for (int i = 0; i < 256; ++i) maxv = std::max(maxv, hist[i]);
  if (maxv != 0) {
    for (int i = 0; i < 256; ++i) {
      out256[i] = static_cast<int32_t>(
          (static_cast<uint64_t>(hist[i]) * 100 + maxv / 2) / maxv);
    }
  }

  const auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

}  // namespace

double hist_compute_rgba(const uint8_t* rgba, int32_t width, int32_t height,
                         int32_t* out256) {
  // 默认：多线程标量 double，与标准公式 bin-exact（app 主流程用）。
  return compute(rgba, width, height, out256, HIST_IMPL_SCALAR_MT);
}

double hist_compute_rgba_impl(const uint8_t* rgba, int32_t width,
                              int32_t height, int32_t* out256, int32_t impl) {
  return compute(rgba, width, height, out256, impl);
}

double hist_compute_gray(const uint8_t* gray, int32_t width, int32_t height,
                         int32_t row_stride, int32_t* out256) {
  if (out256 != nullptr) std::memset(out256, 0, 256 * sizeof(int32_t));
  if (gray == nullptr || out256 == nullptr || width <= 0 || height <= 0) {
    return 0.0;
  }
  if (row_stride < width) row_stride = width;

  const auto t0 = std::chrono::steady_clock::now();

  // 灰度平面：Y 值即 bin 索引，直接计数（无需 RGB 转换）。相机帧不大，单线程足够。
  uint32_t hist[256];
  std::memset(hist, 0, sizeof(hist));
  for (int32_t y = 0; y < height; ++y) {
    const uint8_t* p = gray + static_cast<size_t>(y) * row_stride;
    for (int32_t x = 0; x < width; ++x) ++hist[p[x]];
  }

  uint32_t maxv = 0;
  for (int i = 0; i < 256; ++i) maxv = std::max(maxv, hist[i]);
  if (maxv != 0) {
    for (int i = 0; i < 256; ++i) {
      out256[i] = static_cast<int32_t>(
          (static_cast<uint64_t>(hist[i]) * 100 + maxv / 2) / maxv);
    }
  }

  const auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}
