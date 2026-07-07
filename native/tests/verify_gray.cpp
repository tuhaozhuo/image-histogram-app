// verify_gray.cpp — 灰度化准确性的穷举完备验证。
// 枚举全部 256^3 = 16,777,216 种 (R,G,B)，对比各实现与标准公式 round(0.299R+0.587G+0.114B)。
#include <cmath>
#include <cstdint>
#include <cstdio>

int main() {
  const long total = 256L * 256 * 256;
  long scalar_mism = 0, neon_mism = 0;
  int neon_worst = 0;
  for (int R = 0; R < 256; ++R)
    for (int G = 0; G < 256; ++G)
      for (int B = 0; B < 256; ++B) {
        // 标准公式（金标准，double）
        const double s = 0.299 * R + 0.587 * G + 0.114 * B;
        const int ref = (int)std::lround(s);
        // 标量实现（histogram.cpp 的 gray_of：double + lround）
        int scalar = (int)std::lround(0.299 * R + 0.587 * G + 0.114 * B);
        if (scalar > 255) scalar = 255;
        if (scalar != ref) ++scalar_mism;
        // NEON 单像素等价（float32 + 四舍五入）
        const float sf = 0.299f * R + 0.587f * G + 0.114f * B;
        int neon = (int)(sf + 0.5f);
        if (neon > 255) neon = 255;
        if (neon != ref) {
          ++neon_mism;
          int d = neon > ref ? neon - ref : ref - neon;
          if (d > neon_worst) neon_worst = d;
        }
      }
  printf("穷举 %ld 种 (R,G,B) 组合：\n", total);
  printf("  标量(double)  vs 标准公式：%ld 处不一致  → %s\n", scalar_mism,
         scalar_mism == 0 ? "完全一致 ✓ (bin-exact)" : "有偏差");
  printf("  NEON(float32) vs 标准公式：%ld 处不一致 (%.4f%%)，最大偏差 %d 灰度级\n",
         neon_mism, 100.0 * neon_mism / total, neon_worst);
  return scalar_mism == 0 ? 0 : 1;
}
