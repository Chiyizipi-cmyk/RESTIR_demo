"""指标计算：MSE / PSNR（线性 HDR 空间 + tonemap 参考空间）。"""
import numpy as np


def mse(a, b):
    """均方误差，a、b 形状 (H,W,3)。"""
    d = a - b
    return float(np.mean(d * d))


def psnr_from_mse(m, peak):
    return float(10.0 * np.log10(peak * peak / m)) if m > 0 else float("inf")


def evaluate(img, gt):
    """返回 dict：线性空间 MSE/PSNR（峰值=GT 99.9 分位数）与 tonemap 空间 MSE/PSNR（峰值=1）。"""
    from pfm_io import tonemap

    m_lin = mse(img, gt)
    peak = float(np.quantile(gt, 0.999))
    peak = max(peak, 1e-6)
    p_lin = psnr_from_mse(m_lin, peak)

    m_tone = mse(tonemap(img), tonemap(gt))
    p_tone = psnr_from_mse(m_tone, 1.0)
    return {
        "mse_linear": m_lin,
        "psnr_linear": p_lin,
        "mse_tonemap": m_tone,
        "psnr_tonemap": p_tone,
        "gt_peak": peak,
    }
