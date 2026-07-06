// histogram.h — 图像灰度直方图计算核心 C ABI
//
// 平台无关。同一份实现供 macOS bench / Android NDK / iOS 编译复用。
// 详见项目 CLAUDE.md 的「计时口径」「精度口径」。
#ifndef HISTOGRAM_H
#define HISTOGRAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 计算 RGBA8888 图像的灰度直方图并归一化到 0..100。
//
// 参数：
//   rgba   : 指向 RGBA8888 连续像素缓冲（长度 = width*height*4）。
//            Flutter dart:ui 解码 toByteData(rgba8888) 后即为该格式。
//   width  : 图像宽（像素）
//   height : 图像高（像素）
//   out256 : 调用方分配的 int32[256]，写入各灰度级归一化后的值（0..100）。
//
// 返回值：仅「统计 + 归一化」阶段的耗时（毫秒，double）。
//         不含解码 / 数据拷贝 / 绘制 —— 对齐评分计时口径。
//
// 说明：灰度化用 Q16 定点四舍五入，与 round(R*.299+G*.587+B*.114) 一致。
//       max==0（纯黑图）时 out256 全部为 0，不除零。
//       width/height <= 0 或 rgba/out256 为空时返回 0，out256 清零（若非空）。
double hist_compute_rgba(const uint8_t* rgba, int32_t width, int32_t height,
                         int32_t* out256);

#ifdef __cplusplus
}
#endif

#endif  // HISTOGRAM_H
