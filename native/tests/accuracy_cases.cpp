// accuracy_cases.cpp — 已知输入的准确性用例，每个用例的期望值可手算对照。
// 覆盖：灰度公式各通道、归一化、多峰、边界、透明图 alpha 处理。
#include "../histogram.h"

#include <cstdint>
#include <cstdio>
#include <vector>

static int g_fail = 0;

// 构造纯色 RGBA 图，跑核心，返回归一化结果。
static void fill(std::vector<uint8_t>& buf, int n, uint8_t r, uint8_t g,
                 uint8_t b, uint8_t a) {
  buf.assign((size_t)n * 4, 0);
  for (int i = 0; i < n; i++) {
    buf[i * 4] = r; buf[i * 4 + 1] = g; buf[i * 4 + 2] = b; buf[i * 4 + 3] = a;
  }
}

// 检查：唯一非零 bin 应在 expect_bin 且值为 100。
static void check_single(const char* name, uint8_t r, uint8_t g, uint8_t b,
                         uint8_t a, int expect_bin) {
  const int n = 4096;
  std::vector<uint8_t> buf;
  fill(buf, n, r, g, b, a);
  int32_t out[256];
  hist_compute_rgba(buf.data(), 64, 64, out);
  int nz = -1, nzcount = 0;
  for (int i = 0; i < 256; i++) if (out[i] != 0) { nz = i; nzcount++; }
  bool ok = (nzcount == 1 && nz == expect_bin && out[expect_bin] == 100);
  printf("  [%s] %-18s RGB(%3d,%3d,%3d) a=%3d → 期望 bin%d=100 | 实测 bin%d=%d %s\n",
         ok ? "PASS" : "FAIL", name, r, g, b, a, expect_bin, nz,
         nz >= 0 ? out[nz] : 0, ok ? "✓" : "✗");
  if (!ok) g_fail++;
}

int main() {
  printf("=== 准确性用例（已知输入，期望值可手算）===\n\n");

  printf("【1】灰度公式各通道（gray = round(R*0.299+G*0.587+B*0.114)）\n");
  check_single("纯黑", 0, 0, 0, 255, 0);        // 0
  check_single("纯白", 255, 255, 255, 255, 255); // 255*1.0=255
  check_single("纯红", 255, 0, 0, 255, 76);      // 255*0.299=76.245→76
  check_single("纯绿", 0, 255, 0, 255, 150);     // 255*0.587=149.685→150
  check_single("纯蓝", 0, 0, 255, 255, 29);      // 255*0.114=29.07→29
  check_single("中灰128", 128, 128, 128, 255, 128); // 128*1.0=128

  printf("\n【2】透明图 alpha 处理（应忽略 alpha，只看 RGB）\n");
  check_single("半透红a=128", 255, 0, 0, 128, 76); // 与不透红同为 76（核心忽略 alpha）
  check_single("全透红a=0", 255, 0, 0, 0, 76);     // 同上

  printf("\n【3】归一化（out[i] = round(count[i]/max*100)）\n");
  {
    // 黑白各半：hist[0]=hist[255]=n/2，max=n/2 → out[0]=out[255]=100
    const int n = 2000;
    std::vector<uint8_t> buf((size_t)n * 4, 0);
    for (int i = 0; i < n; i++) {
      uint8_t v = (i < n / 2) ? 0 : 255;
      buf[i*4]=v; buf[i*4+1]=v; buf[i*4+2]=v; buf[i*4+3]=255;
    }
    int32_t out[256];
    hist_compute_rgba(buf.data(), 40, 50, out);
    bool ok = (out[0] == 100 && out[255] == 100);
    printf("  [%s] 黑白各半 → 期望 bin0=100 & bin255=100 | 实测 %d,%d %s\n",
           ok ? "PASS" : "FAIL", out[0], out[255], ok ? "✓" : "✗");
    if (!ok) g_fail++;
  }
  {
    // 比例归一化：gray=100 出现 1000 次，gray=50 出现 250 次
    // max=1000 → out[100]=100，out[50]=round(250/1000*100)=25
    const int n = 1250;
    std::vector<uint8_t> buf((size_t)n * 4, 0);
    for (int i = 0; i < n; i++) {
      uint8_t v = (i < 1000) ? 100 : 50;
      buf[i*4]=v; buf[i*4+1]=v; buf[i*4+2]=v; buf[i*4+3]=255;
    }
    int32_t out[256];
    hist_compute_rgba(buf.data(), 50, 25, out);
    bool ok = (out[100] == 100 && out[50] == 25);
    printf("  [%s] 比例(1000:250) → 期望 bin100=100 & bin50=25 | 实测 %d,%d %s\n",
           ok ? "PASS" : "FAIL", out[100], out[50], ok ? "✓" : "✗");
    if (!ok) g_fail++;
  }

  printf("\n【4】边界（防崩溃 / 防除零）\n");
  {
    int32_t out[256];
    double ms = hist_compute_rgba(nullptr, 0, 0, out); // 空图
    bool ok = true;
    for (int i = 0; i < 256; i++) if (out[i] != 0) ok = false;
    printf("  [%s] 空图(max==0) → 全 0、不除零、不崩溃 (%.2fms) %s\n",
           ok ? "PASS" : "FAIL", ms, ok ? "✓" : "✗");
    if (!ok) g_fail++;
  }

  printf("\n=================\n");
  printf("结果：%s（%d 个用例失败）\n", g_fail == 0 ? "全部 PASS ✓" : "有失败", g_fail);
  return g_fail == 0 ? 0 : 1;
}
