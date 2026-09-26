"""PFM 读取：restir-gi 输出的 PFM 为 `PF\\nW H\\n-1.0\\n` + float32 RGB 小端，顶行在前。"""
import numpy as np


def read_pfm(path):
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"PF":
            raise ValueError(f"{path}: not a color PFM (magic={magic!r})")
        dims = f.readline().split()
        w, h = int(dims[0]), int(dims[1])
        scale = float(f.readline())  # -1.0 = little-endian
        endian = "<" if scale < 0 else ">"
        data = np.fromfile(f, endian + "f4", w * h * 3)
    # 渲染器约定：首行即图像顶行，Python 侧无需翻转
    return data.reshape(h, w, 3).astype(np.float64)


def tonemap(img, exposure=0.0, gamma=2.2):
    """简单 Reinhard + gamma，用于对比图排版（不参与指标计算）。"""
    x = img * (2.0 ** exposure)
    x = x / (1.0 + x)
    return np.clip(x, 0.0, 1.0) ** (1.0 / gamma)
