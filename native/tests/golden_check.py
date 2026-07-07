#!/usr/bin/env python3
"""金标准对拍：用 numpy 独立算标准灰度直方图，与本项目实现交叉验证。

用完全独立的工具链（Python/numpy）复算，排除 C++ 实现的系统性错误。
取整口径与 C++ 严格对齐（四舍五入 round-half-up + 整数归一化）。

依赖：pip install numpy pillow
用法：
  python3 golden_check.py <image>                       # 打印金标准 256 值 (CSV)
  python3 golden_check.py <image> --ascii               # 附 ASCII 直方图
  ./bench --dump <image> > ours.csv
  python3 golden_check.py <image> --compare ours.csv    # 与本项目结果逐 bin 对拍
"""
import sys
import numpy as np
from PIL import Image


def golden(path):
    img = np.asarray(Image.open(path).convert('RGB')).astype(np.float64)
    R, G, B = img[..., 0], img[..., 1], img[..., 2]
    # 标准公式 + 四舍五入（round-half-up，与 C++ std::lround 对正数一致）
    gray = np.floor(0.299 * R + 0.587 * G + 0.114 * B + 0.5).astype(np.int64)
    gray = np.clip(gray, 0, 255)
    hist = np.bincount(gray.ravel(), minlength=256).astype(np.int64)
    m = int(hist.max())
    if m == 0:
        return np.zeros(256, dtype=np.int64)
    return (hist * 100 + m // 2) // m   # 整数归一化，匹配 C++


def ascii_hist(norm):
    rows, step = 20, 2
    for row in range(rows, 0, -1):
        th = row * 100 // rows
        line = ''.join('#' if max(norm[c * step:c * step + step]) >= th else ' '
                       for c in range(256 // step))
        print('  |' + line)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    g = golden(sys.argv[1])
    if '--compare' in sys.argv:
        f = sys.argv[sys.argv.index('--compare') + 1]
        ours = np.array([int(x) for x in open(f).read().strip().split(',')])
        mism = int((g != ours).sum())
        print(f'逐 bin 对拍：{256 - mism}/256 一致，{mism} 处不同',
              '→ PASS ✓' if mism == 0 else '→ 有差异')
        for i in range(256):
            if g[i] != ours[i]:
                print(f'  bin {i}: golden={g[i]} ours={ours[i]}')
    else:
        print(','.join(map(str, g.tolist())))
        if '--ascii' in sys.argv:
            ascii_hist(g)


if __name__ == '__main__':
    main()
