"""生成 docs/02-performance.md 所需全部图表（AGENT.md §7.3）。

用法（仓库根目录，需先跑完对应实验）：
  python tools/make_plots.py e1|e3|e4|e5|e6|all

输出至 results/figs/：
  e1_<scene>_mse_time.png / e1_<scene>_mse_rays.png   — E1 收敛曲线（时间/射线两口径）
  e2_budget_table.png                                  — E2 等预算对比柱状图（源自 E1 数据）
  e3_heatmap_mse.png / e3_heatmap_time.png             — E3 半径×候选热力图
  e4_compare_s<spp>.png                                — E4 四宫格（PT/unbiased/biased/漏光掩码）
  quad_<scene>.png                                     — 四宫格对比图（GT/PT/ReSTIR无复用/ReSTIR复用 + 局部放大）
  e5_stability.png                                     — E5 多种子 MSE 分布
  e6_scaling.png                                       — E6 三角形数-时间/射线曲线
"""
import csv
import json
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pfm_io import read_pfm, tonemap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS = os.path.join(ROOT, "results")
FIGS = os.path.join(RESULTS, "figs")
os.makedirs(FIGS, exist_ok=True)

SCENES = ["cornell", "occlusion", "hdr"]
SCENE_CN = {"cornell": "S1 Cornell Box", "occlusion": "S2 Occlusion", "hdr": "S3 HDR Small Light"}
TAG_LABEL = {"pt": "PT (NEE)", "restir_noreuse": "ReSTIR 无复用",
             "restir_reuse_unbiased": "ReSTIR 空间复用 (unbiased)",
             "unbiased": "ReSTIR unbiased", "biased": "ReSTIR biased", "restir": "ReSTIR"}
TAG_STYLE = {"pt": dict(marker="o", color="#d62728"),
             "restir_noreuse": dict(marker="s", color="#1f77b4"),
             "restir_reuse_unbiased": dict(marker="^", color="#2ca02c"),
             "unbiased": dict(marker="^", color="#2ca02c"),
             "biased": dict(marker="v", color="#9467bd"),
             "restir": dict(marker="^", color="#2ca02c")}

plt.rcParams["font.size"] = 10
plt.rcParams["axes.grid"] = True
plt.rcParams["grid.alpha"] = 0.3
plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False


def load_summary(exp):
    path = os.path.join(RESULTS, exp, f"{exp}_summary.csv")
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            for k in ("spp", "seed", "total_rays"):
                r[k] = int(float(r[k]))
            for k in ("time_render_mean", "time_render_std", "rays_per_pixel",
                      "mse_linear", "mse_median", "mse_std", "psnr_linear"):
                r[k] = float(r[k]) if r.get(k) else None
            rows.append(r)
    return rows


# ------------------------------- E1 -------------------------------
def plot_e1():
    """MSE-时间 / MSE-射线数 收敛曲线。
    实线=多种子中位数（典型水平），虚线=均值（含重尾离群），阴影=均值±std。"""
    rows = load_summary("e1")
    for scene in SCENES:
        fig, axes = plt.subplots(1, 2, figsize=(11, 4.4))
        for tag in ["pt", "restir_noreuse", "restir_reuse_unbiased"]:
            sel = sorted([r for r in rows if r["scene"] == scene and tag in r["name"]],
                         key=lambda r: r["spp"])
            if not sel:
                continue
            t = [r["time_render_mean"] for r in sel]
            rays = [r["rays_per_pixel"] for r in sel]
            med = [r["mse_median"] for r in sel]
            mean = [r["mse_linear"] for r in sel]
            lbl = TAG_LABEL[tag]
            for ax, xs, xl in [(axes[0], t, "渲染时间 (s)"), (axes[1], rays, "每像素射线数")]:
                ax.plot(xs, med, linewidth=1.8, label=lbl + " (中位)", **TAG_STYLE[tag])
                ax.plot(xs, mean, linewidth=1.0, linestyle="--", alpha=0.65,
                        label=lbl + " (均值)", color=TAG_STYLE[tag]["color"])
            ax = None
        for ax, xlabel in zip(axes, ["渲染时间 (s)", "每像素射线数"]):
            ax.set_xscale("log")
            ax.set_yscale("log")
            ax.set_xlabel(xlabel)
            ax.set_ylabel("MSE (linear HDR)")
            ax.legend(fontsize=7, ncol=2)
        axes[0].set_title(f"{SCENE_CN[scene]} — MSE vs 时间")
        axes[1].set_title(f"{SCENE_CN[scene]} — MSE vs 射线数")
        fig.tight_layout()
        fig.savefig(os.path.join(FIGS, f"e1_{scene}_mse.png"), dpi=150)
        plt.close(fig)
        print(f"[fig] e1_{scene}_mse.png")


def plot_e2():
    """E2 等时间预算对比（直接读取 e2_budget.csv 的 time 口径）。
    以 ReSTIR 各 spp 配置点为锚，在 PT 曲线上同预算插值 MSE。"""
    path = os.path.join(RESULTS, "e2", "e2_budget.csv")
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            r["budget"] = float(r["budget"])
            r["mse_restir_reuse"] = float(r["mse_restir_reuse"])
            r["mse_pt_same_budget"] = float(r["mse_pt_same_budget"]) if r["mse_pt_same_budget"] else None
            r["gain_pt_over_restir"] = float(r["gain_pt_over_restir"]) if r["gain_pt_over_restir"] else None
            rows.append(r)
    fig, axes = plt.subplots(1, 3, figsize=(14, 4))
    for ax, scene in zip(axes, SCENES):
        sel = [r for r in rows if r["scene"] == scene and r["budget_kind"] == "time"
               and r["mse_pt_same_budget"] is not None]
        sel.sort(key=lambda r: r["budget"])
        if not sel:
            continue
        x = np.arange(len(sel))
        w = 0.36
        ax.bar(x - w / 2, [r["mse_pt_same_budget"] for r in sel], w, label="PT", color="#d62728")
        ax.bar(x + w / 2, [r["mse_restir_reuse"] for r in sel], w, label="ReSTIR 复用", color="#2ca02c")
        for i, r in enumerate(sel):
            g = r["gain_pt_over_restir"]
            if g is not None:
                txt = f"×{g:.1f}" if g >= 1 else f"×{g:.2f}"
                ax.text(i, max(r["mse_pt_same_budget"], r["mse_restir_reuse"]) * 1.25,
                        txt, ha="center", fontsize=8)
        ax.set_xticks(x)
        ax.set_xticklabels([r["anchor"].replace("restir_s", "spp=") for r in sel], fontsize=8)
        ax.set_yscale("log")
        ax.set_ylabel("MSE (linear)")
        ax.set_title(f"{SCENE_CN[scene]} 等时间预算")
        ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(FIGS, "e2_budget.png"), dpi=150)
    plt.close(fig)
    print("[fig] e2_budget.png")


# ------------------------------- E3 -------------------------------
def plot_e3():
    rows = load_summary("e3")
    radii = [1, 2, 3, 5]
    cands = [1, 3, 5, 8]
    mse = np.full((len(radii), len(cands)), np.nan)
    tim = np.full_like(mse, np.nan)
    for r in rows:
        i = radii.index(int(r["spatial_radius"]))
        j = cands.index(int(r["candidates_per_pixel"]))
        mse[i, j] = r["mse_linear"]
        tim[i, j] = r["time_render_mean"]
    fig, axes = plt.subplots(1, 2, figsize=(11, 4))
    for ax, mat, title, fmt in [
        (axes[0], mse, "MSE (linear HDR)", "%.2e"),
        (axes[1], tim, "渲染时间 (s)", "%.3f"),
    ]:
        im = ax.imshow(mat, cmap="viridis_r" if mat is mse else "viridis",
                       origin="lower", aspect="auto")
        ax.set_xticks(range(len(cands)), cands)
        ax.set_yticks(range(len(radii)), radii)
        ax.set_xlabel("每像素候选数")
        ax.set_ylabel("空间半径")
        ax.set_title(title)
        for i in range(len(radii)):
            for j in range(len(cands)):
                ax.text(j, i, fmt % mat[i, j], ha="center", va="center",
                        color="white", fontsize=7)
        fig.colorbar(im, ax=ax, shrink=0.85)
    fig.suptitle("E3 复用参数扫描（S1 Cornell, spp=4, unbiased）")
    fig.tight_layout()
    fig.savefig(os.path.join(FIGS, "e3_heatmap.png"), dpi=150)
    plt.close(fig)
    print("[fig] e3_heatmap.png")


# ------------------------------- E4 -------------------------------
def plot_e4():
    gt = read_pfm(os.path.join(RESULTS, "gt", "occlusion_gt.pfm"))
    vmax = np.quantile(tonemap(gt, exposure=3.0), 0.995)  # GT 高亮基准，统一 +1.5EV 展示
    for spp in [4, 16]:
        fig, axes = plt.subplots(1, 5, figsize=(18, 3.6))
        panels = [
            ("GT (PT 4096spp)", os.path.join(RESULTS, "gt", "occlusion_gt.pfm"), None),
            (f"PT spp={spp}", os.path.join(RESULTS, "e4", f"occlusion_pt_s{spp}.pfm"), None),
            (f"ReSTIR unbiased spp={spp}", os.path.join(RESULTS, "e4", f"occlusion_unbiased_s{spp}.pfm"), None),
            (f"ReSTIR biased spp={spp}", os.path.join(RESULTS, "e4", f"occlusion_biased_s{spp}.pfm"), None),
            ("漏光掩码 (biased)", None, os.path.join(RESULTS, "e4", f"leakmask_s{spp}.npy")),
        ]
        for ax, (title, pfm, mask) in zip(axes, panels):
            if pfm:
                ax.imshow(np.clip(tonemap(read_pfm(pfm), exposure=3.0) / vmax, 0, 1))
            else:
                ax.imshow(np.load(mask), cmap="hot")
            ax.set_title(title, fontsize=9)
            ax.axis("off")
        fig.suptitle(f"E4 S2 遮挡场景 biased/unbiased 对比 (spp={spp})", fontsize=11)
        fig.tight_layout()
        fig.savefig(os.path.join(FIGS, f"e4_compare_s{spp}.png"), dpi=150)
        plt.close(fig)
        print(f"[fig] e4_compare_s{spp}.png")


# --------------------------- 四宫格对比图 ---------------------------
def plot_quads():
    """每场景一张：GT / PT / ReSTIR无复用 / ReSTIR复用(unbiased)，spp=4，含局部放大行。"""
    for scene in SCENES:
        gt = read_pfm(os.path.join(RESULTS, "gt", f"{scene}_gt.pfm"))
        paths = [
            ("GT (PT 4096spp)", os.path.join(RESULTS, "gt", f"{scene}_gt.pfm")),
            ("PT spp=4", os.path.join(RESULTS, "e1", f"{scene}_pt_s4.pfm")),
            ("ReSTIR 无复用 spp=4", os.path.join(RESULTS, "e1", f"{scene}_restir_noreuse_s4.pfm")),
            ("ReSTIR 复用 spp=4", os.path.join(RESULTS, "e1", f"{scene}_restir_reuse_unbiased_s4.pfm")),
        ]
        # 局部放大窗口：图像中心 1/4 区域
        h, w = gt.shape[:2]
        y0, y1 = h // 4, h * 3 // 4
        x0, x1 = w // 4, w * 3 // 4
        vmax = np.quantile(tonemap(gt, exposure=3.0), 0.995)  # GT 高亮基准，统一 +1.5EV 展示
        fig, axes = plt.subplots(2, 4, figsize=(15, 7.2))
        for col, (title, p) in enumerate(paths):
            img = np.clip(tonemap(read_pfm(p), exposure=3.0) / vmax, 0, 1)
            axes[0, col].imshow(img)
            axes[0, col].set_title(title, fontsize=9)
            axes[1, col].imshow(img[y0:y1, x0:x1])
            axes[1, col].set_title("局部放大", fontsize=8)
            for r in (0, 1):
                axes[r, col].axis("off")
        fig.suptitle(f"{SCENE_CN[scene]} — 同 spp=4 四宫格对比", fontsize=11)
        fig.tight_layout()
        fig.savefig(os.path.join(FIGS, f"quad_{scene}.png"), dpi=150)
        plt.close(fig)
        print(f"[fig] quad_{scene}.png")


# ------------------------------- E5 -------------------------------
def plot_e5():
    stats = {}
    for tag in ["pt", "restir"]:
        with open(os.path.join(RESULTS, "e5", f"e5_{tag}_stats.json")) as f:
            stats[tag] = json.load(f)
    fig, ax = plt.subplots(figsize=(6.5, 4.2))
    for i, tag in enumerate(["pt", "restir"]):
        s = stats[tag]
        mses = np.array(s["mses"])
        ax.scatter(np.full_like(mses, i), mses, zorder=3,
                   color=TAG_STYLE[tag]["color"], alpha=0.7)
        ax.errorbar([i], [mses.mean()], yerr=[mses.std()],
                    fmt="k_", capsize=6, elinewidth=2, zorder=4)
        ax.text(i + 0.08, mses.mean(), f"mean={mses.mean():.3e}\nCV={mses.std()/mses.mean()*100:.1f}%",
                fontsize=9, va="center")
    ax.set_xticks([0, 1], ["PT (NEE)", "ReSTIR 空间复用"])
    ax.set_yscale("log")
    ax.set_ylabel("MSE (linear HDR)")
    ax.set_xlim(-0.4, 1.9)
    ax.set_title("E5 多种子稳定性（S1 Cornell, spp=4, 8 种子）")
    fig.tight_layout()
    fig.savefig(os.path.join(FIGS, "e5_stability.png"), dpi=150)
    plt.close(fig)
    print("[fig] e5_stability.png")


# ------------------------------- E6 -------------------------------
def plot_e6():
    rows = load_summary("e6")
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.2))
    for tag, label in [("pt", "PT (NEE)"), ("restir", "ReSTIR 空间复用")]:
        sel = sorted([r for r in rows if r["name"].endswith(tag)],
                     key=lambda r: int(r["triangles"]))
        tris = [int(r["triangles"]) for r in sel]
        t = [r["time_render_mean"] for r in sel]
        rays = [r["rays_per_pixel"] for r in sel]
        axes[0].plot(tris, t, label=label, linewidth=1.6, **TAG_STYLE[tag])
        axes[1].plot(tris, rays, label=label, linewidth=1.6, **TAG_STYLE[tag])
    axes[0].set_xscale("log")
    axes[0].set_xlabel("三角形数量")
    axes[0].set_ylabel("渲染时间 (s)")
    axes[0].set_title("时间 vs 场景复杂度 (spp=4)")
    axes[0].legend(fontsize=8)
    axes[1].set_xscale("log")
    axes[1].set_xlabel("三角形数量")
    axes[1].set_ylabel("每像素射线数")
    axes[1].set_title("射线数 vs 场景复杂度 (spp=4)")
    axes[1].legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(FIGS, "e6_scaling.png"), dpi=150)
    plt.close(fig)
    print("[fig] e6_scaling.png")


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which in ("e1", "all"):
        plot_e1(); plot_e2(); plot_quads()
    if which in ("e3", "all"):
        plot_e3()
    if which in ("e4", "all"):
        plot_e4()
    if which in ("e5", "all"):
        plot_e5()
    if which in ("e6", "all"):
        plot_e6()


if __name__ == "__main__":
    main()
