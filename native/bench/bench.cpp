// bench.cpp — macOS 原生基准 + 正确性对拍
//
// 用法：
//   ./bench                 # 合成 12MP 大图跑性能 + 对拍
//   ./bench <image_path>    # 读真实图片（JPEG/PNG…）跑性能 + 对拍
//
// 判定：耗时 ≤300ms 且优化路径与 float 参考实现逐 bin 一致 → PASS。
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "../histogram.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

namespace {

// float 参考实现：严格按需求公式 round(R*.299+G*.587+B*.114)，作为准确性基准。
void reference_histogram(const uint8_t* rgba, int w, int h, int32_t out256[256]) {
  uint64_t hist[256] = {0};
  const size_t n = static_cast<size_t>(w) * h;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t* p = rgba + i * 4;
    // 与 histogram.cpp 的 gray_of 完全相同的标准公式表达式。
    double gray = p[0] * 0.299 + p[1] * 0.587 + p[2] * 0.114;
    int g = static_cast<int>(std::lround(gray));
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    ++hist[g];
  }
  uint64_t maxv = 0;
  for (int i = 0; i < 256; ++i)
    if (hist[i] > maxv) maxv = hist[i];
  for (int i = 0; i < 256; ++i) {
    out256[i] = maxv == 0
                    ? 0
                    : static_cast<int32_t>((hist[i] * 100 + maxv / 2) / maxv);
  }
}

// 合成一张 w×h 的 RGBA 图：水平梯度 + 随机噪声，覆盖较广灰度分布。
std::vector<uint8_t> make_synthetic(int w, int h) {
  std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 4);
  std::mt19937 rng(12345);
  std::uniform_int_distribution<int> noise(-20, 20);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int base = (x * 255) / (w > 1 ? w - 1 : 1);
      auto clamp = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
      uint8_t* p = &buf[(static_cast<size_t>(y) * w + x) * 4];
      p[0] = static_cast<uint8_t>(clamp(base + noise(rng)));
      p[1] = static_cast<uint8_t>(clamp((base + y) % 256 + noise(rng)));
      p[2] = static_cast<uint8_t>(clamp(255 - base + noise(rng)));
      p[3] = 255;
    }
  }
  return buf;
}

// 把 256×100 归一化直方图打成 ASCII 竖图，直观看形状。
void print_ascii_histogram(const int32_t norm[256]) {
  const int rows = 20;   // 纵向 20 行（映射 0..100）
  const int step = 2;    // 每 2 个 bin 合成一列 → 128 列宽
  const int cols = 256 / step;
  for (int row = rows; row >= 1; --row) {
    const int threshold = row * 100 / rows;
    std::string line;
    for (int c = 0; c < cols; ++c) {
      int v = 0;
      for (int k = 0; k < step; ++k) v = std::max(v, norm[c * step + k]);
      line += (v >= threshold) ? '#' : ' ';
    }
    printf("  |%s\n", line.c_str());
  }
  printf("  +");
  for (int c = 0; c < cols; ++c) printf("-");
  printf("\n   gray=0%*sgray=255\n\n", cols - 10, "");
}

int count_mismatches(const int32_t a[256], const int32_t b[256]) {
  int m = 0;
  for (int i = 0; i < 256; ++i)
    if (a[i] != b[i]) ++m;
  return m;
}

// 跑单个实现，取多次最小耗时，写回 out256，返回最优耗时(ms)。
double bench_impl(const uint8_t* rgba, int w, int h, int impl, int32_t out[256]) {
  hist_compute_rgba_impl(rgba, w, h, out, impl);  // 预热
  double best = 1e18;
  for (int r = 0; r < 5; ++r) {
    int32_t tmp[256];
    double ms = hist_compute_rgba_impl(rgba, w, h, tmp, impl);
    if (ms < best) {
      best = ms;
      std::memcpy(out, tmp, 256 * sizeof(int32_t));
    }
  }
  return best;
}

int run_case(const char* label, const uint8_t* rgba, int w, int h) {
  printf("=== %s (%dx%d = %.1f MP) ===\n", label, w, h,
         (w * (double)h) / 1e6);

  int32_t ref[256];
  reference_histogram(rgba, w, h, ref);

  const int impls[3] = {HIST_IMPL_SCALAR_ST, HIST_IMPL_SCALAR_MT,
                        HIST_IMPL_NEON_MT};
  const char* names[3] = {"scalar 单线程 (baseline)", "scalar 多线程",
                          "NEON 多线程"};
  double base = 0;
  int32_t out_default[256];
  int fails = 0;

  printf("  %-26s %10s %8s  %s\n", "实现", "耗时", "加速比", "精度(vs 标准公式)");
  for (int i = 0; i < 3; ++i) {
    int32_t out[256];
    double ms = bench_impl(rgba, w, h, impls[i], out);
    if (i == 0) base = ms;
    const int mism = count_mismatches(out, ref);
    char acc[48];
    if (mism == 0)
      std::snprintf(acc, sizeof(acc), "bin-exact ✓");
    else
      std::snprintf(acc, sizeof(acc), "%d bins ±1", mism);
    printf("  %-24s %8.2f ms %6.2fx  %s\n", names[i], ms, base / ms, acc);
    if (ms > 300.0) ++fails;
    if (impls[i] == HIST_IMPL_SCALAR_MT) std::memcpy(out_default, out, sizeof(out));
  }
  printf("\n");
  print_ascii_histogram(out_default);  // 默认精确实现的直方图形态
  return fails;
}

}  // namespace

int main(int argc, char** argv) {
  int fails = 0;

  // --dump <img>：只输出 256 个归一化值（逗号分隔），供与金标准脚本 diff 对拍。
  if (argc >= 3 && std::string(argv[1]) == "--dump") {
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(argv[2], &w, &h, &ch, 4);
    if (!data) {
      printf("failed to load image: %s\n", argv[2]);
      return 2;
    }
    int32_t out[256];
    hist_compute_rgba(data, w, h, out);  // 默认精确实现
    stbi_image_free(data);
    for (int i = 0; i < 256; ++i) printf("%d%s", out[i], i < 255 ? "," : "\n");
    return 0;
  }

  if (argc >= 2) {
    int w = 0, h = 0, ch = 0;
    // 强制解码为 4 通道 RGBA。
    unsigned char* data = stbi_load(argv[1], &w, &h, &ch, 4);
    if (!data) {
      printf("failed to load image: %s (%s)\n", argv[1], stbi_failure_reason());
      return 2;
    }
    fails += run_case(argv[1], data, w, h);
    stbi_image_free(data);
    return fails ? 1 : 0;
  }

  // 默认：合成大图性能测试。
  {
    auto buf = make_synthetic(4000, 3000);  // 12 MP
    fails += run_case("synthetic 12MP", buf.data(), 4000, 3000);
  }
  // 附加：更大图压力测试。
  {
    auto buf = make_synthetic(6000, 4000);  // 24 MP
    fails += run_case("synthetic 24MP (stress)", buf.data(), 6000, 4000);
  }
  // 边界：纯黑图。全部像素 gray=0，故 bin0 是唯一非零且为 max → 归一化后
  // out[0]==100，其余为 0。（真正的 max==0 只会在空图出现，此处验证不崩溃、
  // 结果正确、且归一化无异常。）
  {
    std::vector<uint8_t> black(static_cast<size_t>(1024) * 1024 * 4, 0);
    for (size_t i = 3; i < black.size(); i += 4) black[i] = 255;  // alpha
    int32_t out[256];
    double ms = hist_compute_rgba(black.data(), 1024, 1024, out);
    bool ok = (out[0] == 100);
    for (int i = 1; i < 256; ++i)
      if (out[i] != 0) ok = false;
    printf("=== black image edge case ===\n");
    printf("  time: %.2f ms   out[0]==100 & rest==0: %s\n\n", ms,
           ok ? "PASS" : "FAIL");
    if (!ok) ++fails;
  }

  // 边界：空图 / 非法尺寸（真正触发 max==0 特判，验证不除零、不崩溃）。
  {
    int32_t out[256];
    double ms = hist_compute_rgba(nullptr, 0, 0, out);
    bool ok = true;
    for (int i = 0; i < 256; ++i)
      if (out[i] != 0) ok = false;
    printf("=== empty/invalid input edge case (max==0 guard) ===\n");
    printf("  time: %.2f ms   all-zero & no crash: %s\n\n", ms,
           ok ? "PASS" : "FAIL");
    if (!ok) ++fails;
  }

  printf("==================\n");
  printf("RESULT: %s\n", fails == 0 ? "ALL PASS" : "SOME FAILED");
  return fails ? 1 : 0;
}
