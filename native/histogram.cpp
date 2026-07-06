// histogram.cpp — 灰度直方图核心算法
//
// 优化组合（见 CLAUDE.md）：
//   统计时灰度化（一次遍历）+ 分块多线程 + 局部直方图归并（无锁）。
//
// 精度：严格采用标准公式 round(R*0.299 + G*0.587 + B*0.114)，用预计算查表
//   （每通道 256 项 double 贡献）加速，结果与标准公式逐 bin 完全一致。
//   （曾试 Q16 定点，因半整数 tie 点无法与标准公式 bit 对齐而放弃；速度余量
//   充足，无需牺牲准确性——详见 CLAUDE.md 精度口径。）
#include "histogram.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

namespace {

// 标准灰度公式：gray = round(R*0.299 + G*0.587 + B*0.114)。
// 直写表达式（不用 FMA 融合、不用定点近似），保证与标准公式 double 计算完全
// 一致；配合编译期 -ffp-contract=off，跨平台（Android/iOS）结果确定一致。
inline int gray_of(uint8_t r, uint8_t g, uint8_t b) {
  const double gray = r * 0.299 + g * 0.587 + b * 0.114;
  int gi = static_cast<int>(std::lround(gray));  // 四舍五入
  if (gi > 255) gi = 255;  // 保险 clamp（gray∈[0,255]）
  return gi;
}

// 对 [row_begin, row_end) 行区间统计局部直方图（256 桶）。
void accumulate_rows(const uint8_t* rgba, int32_t width, int32_t row_begin,
                     int32_t row_end, uint32_t* local_hist) {
  std::memset(local_hist, 0, 256 * sizeof(uint32_t));
  const size_t row_stride = static_cast<size_t>(width) * 4;
  for (int32_t y = row_begin; y < row_end; ++y) {
    const uint8_t* p = rgba + static_cast<size_t>(y) * row_stride;
    for (int32_t x = 0; x < width; ++x) {
      ++local_hist[gray_of(p[0], p[1], p[2])];  // p[3]=alpha 忽略
      p += 4;
    }
  }
}

}  // namespace

double hist_compute_rgba(const uint8_t* rgba, int32_t width, int32_t height,
                         int32_t* out256) {
  if (out256 != nullptr) {
    std::memset(out256, 0, 256 * sizeof(int32_t));
  }
  if (rgba == nullptr || out256 == nullptr || width <= 0 || height <= 0) {
    return 0.0;
  }

  const auto t0 = std::chrono::steady_clock::now();

  // ---- 统计（分块多线程 + 局部直方图归并）----
  uint32_t hist[256];
  std::memset(hist, 0, sizeof(hist));

  unsigned hw = std::thread::hardware_concurrency();
  if (hw == 0) hw = 4;
  // 线程数不超过行数，且避免小图开销：每线程至少处理若干行。
  int32_t num_threads = static_cast<int32_t>(hw);
  const int32_t min_rows_per_thread = 64;
  if (height < num_threads * min_rows_per_thread) {
    num_threads = std::max(1, height / min_rows_per_thread);
  }
  if (num_threads < 1) num_threads = 1;

  if (num_threads == 1) {
    accumulate_rows(rgba, width, 0, height, hist);
  } else {
    std::vector<std::thread> threads;
    threads.reserve(num_threads - 1);
    // 每线程一份局部直方图，归并时相加 —— 无共享写、无锁。
    std::vector<std::vector<uint32_t>> locals(
        num_threads, std::vector<uint32_t>(256));
    const int32_t base = height / num_threads;
    const int32_t rem = height % num_threads;  // 余数摊到前 rem 个线程
    int32_t row = 0;
    for (int32_t t = 0; t < num_threads; ++t) {
      const int32_t rows = base + (t < rem ? 1 : 0);
      const int32_t begin = row;
      const int32_t end = row + rows;
      row = end;
      if (t == num_threads - 1) {
        accumulate_rows(rgba, width, begin, end, locals[t].data());
      } else {
        threads.emplace_back(accumulate_rows, rgba, width, begin, end,
                             locals[t].data());
      }
    }
    for (auto& th : threads) th.join();
    for (int32_t t = 0; t < num_threads; ++t) {
      const uint32_t* l = locals[t].data();
      for (int i = 0; i < 256; ++i) hist[i] += l[i];
    }
  }

  // ---- 归一化到 0..100 ----
  uint32_t maxv = 0;
  for (int i = 0; i < 256; ++i) maxv = std::max(maxv, hist[i]);
  if (maxv == 0) {
    // 纯黑等极端情况：全 0，不除零。out256 已在开头清零。
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
  }
  for (int i = 0; i < 256; ++i) {
    // round(count/max*100)：用整数运算避免浮点。
    out256[i] = static_cast<int32_t>(
        (static_cast<uint64_t>(hist[i]) * 100 + maxv / 2) / maxv);
  }

  const auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}
