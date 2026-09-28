"""实验矩阵驱动（AGENT.md §7.2）。

用法（在仓库根目录）：
  python tools/run_experiments.py gt            # 生成 GT（--method pt --spp 4096，固定种子）
  python tools/run_experiments.py e1            # E1 收敛对比（E2 从其数据派生）
  python tools/run_experiments.py e2            # E2 等预算对比（双向插值表）
  python tools/run_experiments.py e3            # E3 复用参数扫描
  python tools/run_experiments.py e4            # E4 偏差分析（S2 biased/unbiased + 漏光量化）
  python tools/run_experiments.py e5            # E5 多种子稳定性
  python tools/run_experiments.py e6            # E6 复杂度扩展
  python tools/run_experiments.py biasfloor     # 偏差分解：PT(max-depth=2) 高采样参考
  python tools/run_experiments.py all           # e1..e6 全部（gt 除外）
  python tools/run_experiments.py list          # 列出矩阵规模（dry-run）

约定：
- 计时口径：time_render_sec（纯渲染）；同配置重复 REPEATS 次，取均值±方差（E5 除外——其本身就是种子重复）。
- GT：results/gt/<scene>_gt.pfm，--method pt --spp 4096 只跑一次；缺失时自动调用。
- 输出：results/<exp>/<name>.{pfm,png,json}，汇总 results/<exp>/<exp>_summary.csv。
- EXR 默认关闭以省 I/O（GT 除外）。
"""
import csv
import json
import math
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from metrics import evaluate, mse, psnr_from_mse
from pfm_io import read_pfm, write_pfm

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "restir-gi.exe")
if not os.path.exists(EXE):
    EXE = os.path.join(ROOT, "build", "restir-gi")
RESULTS = os.path.join(ROOT, "results")
GT_DIR = os.path.join(RESULTS, "gt")

WIDTH = HEIGHT = 128
GT_SPP = 4096
GT_SEED = 4294967295          # GT 主渲染种子（docs/01-design.md §7.1）
GT_SEED_B = 4294967291        # 第二独立渲染种子，仅用于估计 GT 自噪声
REPEATS = 3            # 计时重复次数
E5_SEEDS = [11, 23, 37, 41, 53, 67, 79, 97]  # E5 八个种子
BASE_SEED = 42

SCENES = ["cornell", "occlusion", "hdr"]


def run_render(scene, method, spp, seed=BASE_SEED, out_prefix=None, extra=None,
               write_pfm=True, write_png=True, write_exr=False):
    """调用渲染器一次，返回 json dict。"""
    os.makedirs(os.path.dirname(out_prefix), exist_ok=True)
    cmd = [EXE, "--scene", scene, "--method", method,
           "--spp", str(spp), "--seed", str(seed),
           "--width", str(WIDTH), "--height", str(HEIGHT),
           "--output", out_prefix]
    if not write_pfm:
        cmd.append("--no-pfm")
    if not write_png:
        cmd.append("--no-png")
    if not write_exr:
        cmd.append("--no-exr")
    if extra:
        cmd += [str(x) for x in extra]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0:
        raise RuntimeError(f"渲染失败: {' '.join(cmd)}\n{r.stderr}\n{r.stdout}")
    with open(out_prefix + ".json") as f:
        return json.load(f)


def gt_path(scene):
    return os.path.join(GT_DIR, f"{scene}_gt")


def ensure_gt():
    for scene in SCENES:
        if not os.path.exists(gt_path(scene) + ".pfm"):
            gen_gt()
            return


def load_gt(scene):
    ensure_gt()
    return read_pfm(gt_path(scene) + ".pfm")


class Summary:
    """累积一个实验的汇总行并写 CSV。"""

    COLS = ["name", "scene", "method", "spp", "seed",
            "reuse_spatial", "biased", "spatial_radius", "candidates_per_pixel",
            "primary_rays", "gi_rays", "shadow_nee_rays", "shadow_reuse_rays",
            "total_rays", "rays_per_pixel",
            "time_render_mean", "time_render_std", "time_io_mean",
            "mse_linear", "mse_median", "mse_std", "psnr_linear", "mse_tonemap", "psnr_tonemap",
            "w_mean", "w_max", "w_zero_frac", "m_max"]

    def __init__(self, path):
        self.path = path
        self.rows = []

    def add(self, row):
        self.rows.append(row)

    def write(self):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        with open(self.path, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=self.COLS, extrasaction="ignore")
            w.writeheader()
            for r in self.rows:
                w.writerow(r)
        print(f"[summary] {self.path} ({len(self.rows)} rows)")


def render_timed(scene, method, spp, seed=BASE_SEED, out_prefix=None, extra=None,
                 repeats=REPEATS, write_pfm=True, write_png=True):
    """同一命令重复 repeats 次（首次保留图像，其余次仅计时），返回 (json, t_mean, t_std, io_mean)。"""
    times, iot = [], []
    j = None
    for i in range(repeats):
        j = run_render(scene, method, spp, seed, out_prefix, extra,
                       write_pfm=write_pfm if i == 0 else False,
                       write_png=write_png if i == 0 else False)
        times.append(j["time_render_sec"])
        iot.append(j["time_io_sec"])
    return j, float(np.mean(times)), float(np.std(times)), float(np.mean(iot))


def measure(out_prefix, gt):
    """读 PFM 与 GT 对比；无 PFM（纯计时）则返回空指标。"""
    pfm = out_prefix + ".pfm"
    if not os.path.exists(pfm):
        return {}
    return evaluate(read_pfm(pfm), gt)


def base_row(j, name, t_mean, t_std, io_mean, m):
    row = {k: j[k] for k in ["scene", "method", "spp", "seed", "reuse_spatial",
                             "biased", "spatial_radius", "candidates_per_pixel",
                             "primary_rays", "gi_rays", "shadow_nee_rays",
                             "shadow_reuse_rays", "total_rays", "rays_per_pixel"]}
    row.update({"name": name, "time_render_mean": t_mean, "time_render_std": t_std,
                "time_io_mean": io_mean})
    if j.get("method") == "restir":
        for k in ("w_mean", "w_max", "w_zero_frac", "m_max"):
            if k in j:
                row[k] = j[k]
    row.update(m)
    return row


# ------------------------------- GT -------------------------------
def gen_gt(force=False):
    """GT 生成（AGENT.md §7.1 / docs/01-design.md §7.1）。

    渲染两次独立的 4096 spp（不同种子）：
      - 二者**平均**作为最终参考（等效 8192 spp，噪声约为单次的 1/2）；
      - 二者的 MSE 用于估计 GT 自噪声（作为指标的噪声下界）：
          mse_single = mse(a,b)/2   —— 单次 4096 spp 参考的噪声水平
          mse_avg    = mse(a,b)/4   —— 实际使用的平均参考的噪声水平
    结果写入 results/gt/gt_self.json。
    """
    os.makedirs(GT_DIR, exist_ok=True)
    self_stats = {}
    for scene in SCENES:
        out = gt_path(scene)
        if os.path.exists(out + ".pfm") and not force:
            print(f"[gt] 已存在，跳过 {scene}")
            continue
        print(f"[gt] {scene} spp={GT_SPP} seed={GT_SEED} ...", flush=True)
        j = run_render(scene, "pt", GT_SPP, GT_SEED, out, write_exr=True)
        out_b = out + "_b"
        print(f"[gt] {scene} spp={GT_SPP} seed={GT_SEED_B}（自噪声估计）...", flush=True)
        run_render(scene, "pt", GT_SPP, GT_SEED_B, out_b,
                   write_exr=False, write_png=False, write_pfm=True)
        a = read_pfm(out + ".pfm")
        b = read_pfm(out_b + ".pfm")
        m_split = mse(a, b)
        peak = float(np.quantile(0.5 * (a + b), 0.999))
        write_pfm(out + ".pfm", 0.5 * (a + b))     # 平均后的参考
        os.remove(out_b + ".pfm")
        os.remove(out_b + ".json")
        self_stats[scene] = {
            "spp_per_render": GT_SPP,
            "seeds": [GT_SEED, GT_SEED_B],
            "mse_between_renders": m_split,
            "gt_self_mse_single_4096spp": m_split / 2.0,
            "gt_self_mse_averaged_reference": m_split / 4.0,
            "gt_psnr_single_4096spp": psnr_from_mse(m_split / 2.0, peak),
            "gt_psnr_averaged_reference": psnr_from_mse(m_split / 4.0, peak),
            "gt_mean": float((0.5 * (a + b)).mean()),
            "render_sec_first": j["time_render_sec"],
        }
        print(f"[gt] {scene}: {j['time_render_sec']:.1f}s, rays={j['total_rays']}, "
              f"GT self-MSE(4096spp)={m_split/2:.3e}, self-MSE(avg)={m_split/4:.3e}")
    if self_stats:
        with open(os.path.join(GT_DIR, "gt_self.json"), "w") as f:
            json.dump(self_stats, f, indent=2)
        print(f"[gt] 自噪声统计写入 {os.path.join(GT_DIR, 'gt_self.json')}")


# ------------------------------- E1 -------------------------------
E1_SEEDS = [42, 43, 44, 45]  # E1 各种子取 MSE 均值±std（抑制单种子重尾波动）
E1_CONFIGS = [("pt", "pt", ["--reuse-spatial", "0"]),
              ("restir_noreuse", "restir", ["--reuse-spatial", "0"]),
              ("restir_reuse_unbiased", "restir", ["--reuse-spatial", "1", "--biased", "0"])]


def e1():
    """E1 收敛对比：3 场景 × {pt, restir(无复用), restir(复用,unbiased)} × spp{1,4,16,64}。
    每配置跑 E1_SEEDS 个种子，MSE 取均值±std；计时在首种子上重复 REPEATS 次。
    PFM/PNG 仅保留首种子（seed=42，供四宫格展示）。"""
    sm = Summary(os.path.join(RESULTS, "e1", "e1_summary.csv"))
    for scene in SCENES:
        gt = load_gt(scene)
        for tag, method, extra in E1_CONFIGS:
            for spp in [1, 4, 16, 64]:
                name = f"{scene}_{tag}_s{spp}"
                mses, j0, tm, ts, io = [], None, 0.0, 0.0, 0.0
                for si, seed in enumerate(E1_SEEDS):
                    out = os.path.join(RESULTS, "e1", name if si == 0 else f"{name}_seed{seed}")
                    reps = REPEATS if si == 0 else 1
                    j, tm_, ts_, io_ = render_timed(scene, method, spp, seed=seed,
                                                    out_prefix=out, extra=extra, repeats=reps,
                                                    write_pfm=True, write_png=(si == 0))
                    if si == 0:
                        j0, tm, ts, io = j, tm_, ts_, io_
                    mses.append(measure(out, gt)["mse_linear"])
                    if si > 0:
                        os.remove(out + ".pfm")  # 非首种子只留指标，不留图
                m = measure(os.path.join(RESULTS, "e1", name), gt)
                m["mse_linear"] = float(np.mean(mses))
                m["mse_median"] = float(np.median(mses))
                m["mse_std"] = float(np.std(mses))
                sm.add(base_row(j0, name, tm, ts, io, m))
                print(f"[e1] {name}: mse={np.mean(mses):.3e}±{np.std(mses):.1e} "
                      f"psnr={m['psnr_linear']:.2f} t={tm:.3f}s")
    sm.write()


# ------------------------------- E2 -------------------------------
def _interp_xy(xs, ys, x0):
    """log-log 线性插值：xs/ys 升序，x0 越界返回 None。"""
    xs, ys = np.asarray(xs, float), np.asarray(ys, float)
    if x0 < xs[0] or x0 > xs[-1]:
        return None
    ly = np.interp(math.log(x0), np.log(xs), np.log(ys))
    return float(math.exp(ly))


def e2():
    """E2 等预算对比（从 E1 汇总数据插值，无需新渲染）。
    ReSTIR 的 spp 只缩放直接光照，其时间/射线范围窄于 PT；
    故以 ReSTIR 各 spp 配置点为锚，在 PT 曲线上同预算插值 MSE。
    时间口径与射线口径分别给出。"""
    rows = []
    with open(os.path.join(RESULTS, "e1", "e1_summary.csv")) as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        r["spp"] = int(r["spp"])
        for k in ("time_render_mean", "rays_per_pixel", "mse_linear"):
            r[k] = float(r[k])

    out_rows = []
    for scene in SCENES:
        for budget_kind, key in [("time", "time_render_mean"), ("rays", "rays_per_pixel")]:
            curves = {}
            for tag in ("pt", "restir_noreuse", "restir_reuse_unbiased"):
                sel = sorted([r for r in rows if r["scene"] == scene and tag in r["name"]],
                             key=lambda r: r[key])
                curves[tag] = ([r[key] for r in sel], [r["mse_linear"] for r in sel])
            for spp in [1, 4, 16, 64]:
                anchor = next(r for r in rows if r["scene"] == scene
                              and r["name"] == f"{scene}_restir_reuse_unbiased_s{spp}")
                budget = anchor[key]
                xs_pt, ys_pt = curves["pt"]
                m_pt = _interp_xy(xs_pt, ys_pt, budget)
                entry = {"scene": scene, "budget_kind": budget_kind,
                         "budget": budget, "anchor": f"restir_s{spp}",
                         "mse_restir_reuse": anchor["mse_linear"],
                         "mse_pt_same_budget": m_pt if m_pt is not None else "",
                         "gain_pt_over_restir": (m_pt / anchor["mse_linear"])
                         if m_pt is not None else ""}
                out_rows.append(entry)
                print(f"[e2] {scene} {budget_kind}={budget:.4g} (restir_s{spp}): "
                      f"restir={anchor['mse_linear']:.3e} pt={m_pt if m_pt is None else f'{m_pt:.3e}'}")
    path = os.path.join(RESULTS, "e2")
    os.makedirs(path, exist_ok=True)
    with open(os.path.join(path, "e2_budget.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(out_rows[0].keys()))
        w.writeheader()
        w.writerows(out_rows)
    print(f"[summary] {path}/e2_budget.csv ({len(out_rows)} rows)")


# ------------------------------- E3 -------------------------------
def e3():
    """E3 参数扫描（S1 cornell, spp=4, unbiased 复用）：半径{1,2,3,5}×候选{1,3,5,8}。
    每配置跑 E1_SEEDS 个种子取 MSE 均值±std：单种子的 RIS 估计波动（firefly）
    与半径效应同量级，必须多种子才能分辨（旧版单种子结论不可靠）。
    计时在首种子上重复 REPEATS 次。
    """
    sm = Summary(os.path.join(RESULTS, "e3", "e3_summary.csv"))
    gt = load_gt("cornell")
    for radius in [1, 2, 3, 5]:
        for cand in [1, 3, 5, 8]:
            name = f"cornell_r{radius}_c{cand}"
            out = os.path.join(RESULTS, "e3", name)
            extra = ["--reuse-spatial", "1", "--biased", "0",
                     "--spatial-radius", radius, "--candidates-per-pixel", cand]
            mses, j0, tm, ts, io = [], None, 0.0, 0.0, 0.0
            for si, seed in enumerate(E1_SEEDS):
                p = out if si == 0 else f"{out}_seed{seed}"
                j, tm_, ts_, io_ = render_timed("cornell", "restir", 4, seed=seed,
                                                out_prefix=p, extra=extra,
                                                repeats=REPEATS if si == 0 else 1,
                                                write_png=(si == 0))
                if si == 0:
                    j0, tm, ts, io = j, tm_, ts_, io_
                mses.append(measure(p, gt)["mse_linear"])
                if si > 0:
                    os.remove(p + ".pfm")
            m = measure(out, gt)
            m["mse_linear"] = float(np.mean(mses))
            m["mse_median"] = float(np.median(mses))
            m["mse_std"] = float(np.std(mses))
            sm.add(base_row(j0, name, tm, ts, io, m))
            print(f"[e3] {name}: mse={m['mse_linear']:.3e}±{m['mse_std']:.1e} "
                  f"t={tm:.3f}s rays={j0['total_rays']}")
    sm.write()


# ------------------------------- E4 -------------------------------
def e4():
    """E4 偏差分析（S2 occlusion）。

    (a) spp ∈ {4,16}：PT 基线 / ReSTIR unbiased 复用 / ReSTIR biased 复用。
    (b) 复用半径扫描 r ∈ {2,5,10}（spp=16）：验证漏光随复用范围单调增长。

    漏光指标采用**配对定义**：biased 与 unbiased 两次渲染使用完全相同的邻居选择
    随机流，唯一的差别是 biased 省略了 V(x0,x1') 检测。因此
        Δ = luminance(biased) - luminance(unbiased)
    只反映"漏光"这一项，排除了 firefly / 重尾带来的假阳性
    （旧版用 |biased - GT| 的绝对阈值，实际测到的是噪声而非漏光）。
      leak_pixel_ratio : GT 暗像素（luminance<0.05）中 Δ>0.01 的比例
      leak_excess      : GT 暗像素上 mean(max(0, Δ))（漏光能量密度）
      leak_max         : GT 暗像素上 max(Δ)
    """
    sm = Summary(os.path.join(RESULTS, "e4", "e4_summary.csv"))
    gt = load_gt("occlusion")
    lum_gt = 0.2126 * gt[..., 0] + 0.7152 * gt[..., 1] + 0.0722 * gt[..., 2]
    dark = lum_gt < 0.05

    cases = [("s4", 4, 2), ("s16", 16, 2), ("s16_r5", 16, 5), ("s16_r10", 16, 10)]
    for tag_s, spp, radius in cases:
        imgs = {}
        for tag, method, extra in [
            ("pt", "pt", ["--reuse-spatial", "0"]),
            ("unbiased", "restir", ["--reuse-spatial", "1", "--biased", "0",
                                    "--spatial-radius", str(radius)]),
            ("biased", "restir", ["--reuse-spatial", "1", "--biased", "1",
                                  "--spatial-radius", str(radius)]),
        ]:
            name = f"occlusion_{tag}_{tag_s}"
            out = os.path.join(RESULTS, "e4", name)
            j, tm, ts, io = render_timed("occlusion", method, spp, out_prefix=out, extra=extra)
            m = measure(out, gt)
            if tag != "pt":
                imgs[tag] = read_pfm(out + ".pfm")
            if tag == "biased":
                def lum(a):
                    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
                d = lum(imgs["biased"]) - lum(imgs["unbiased"])
                hit = dark & (d > 0.01)
                m["leak_pixel_ratio"] = float(np.mean(hit))
                m["leak_pixel_count"] = int(np.sum(hit))
                m["leak_excess"] = float(np.mean(np.maximum(d, 0.0)[dark]))
                m["leak_max"] = float(np.max(d[dark]))
                np.save(os.path.join(RESULTS, "e4", f"leakmask_{tag_s}.npy"), hit)
            row = base_row(j, name, tm, ts, io, m)
            row["spatial_radius"] = radius
            sm.add(row)
            print(f"[e4] {name}: mse={m['mse_linear']:.3e} t={tm:.3f}s"
                  + (f" leak_px={m['leak_pixel_count']}({m['leak_pixel_ratio']*100:.3f}%)"
                     f" leak_excess={m['leak_excess']:.2e} leak_max={m['leak_max']:.3f}"
                     if "leak_pixel_ratio" in m else ""))
    sm.COLS += ["leak_pixel_ratio", "leak_pixel_count", "leak_excess", "leak_max"]
    sm.write()


# ------------------------------- E5 -------------------------------
def e5():
    """E5 稳定性：8 种子 × (PT, ReSTIR unbiased) @ S1 spp=4。统计 MSE/时间/W 诊断。"""
    sm = Summary(os.path.join(RESULTS, "e5", "e5_summary.csv"))
    gt = load_gt("cornell")
    for tag, method, extra in [
        ("pt", "pt", ["--reuse-spatial", "0"]),
        ("restir", "restir", ["--reuse-spatial", "1", "--biased", "0"]),
    ]:
        mses, times = [], []
        wstats_rows = []
        for seed in E5_SEEDS:
            name = f"cornell_{tag}_seed{seed}"
            out = os.path.join(RESULTS, "e5", name)
            j, tm, ts, io = render_timed("cornell", method, 4, seed=seed,
                                         out_prefix=out, extra=extra, repeats=1)
            m = measure(out, gt)
            mses.append(m["mse_linear"])
            times.append(tm)
            row = base_row(j, name, tm, ts, io, m)
            for k in ("w_mean", "w_max", "w_zero_frac", "m_max"):
                if k in j:
                    row[k] = j[k]
                    wstats_rows.append(j[k])
            sm.add(row)
        arr = np.array(mses)
        stats = {"method": tag, "seeds": E5_SEEDS, "mses": mses,
                 "mse_mean": float(arr.mean()), "mse_std": float(arr.std()),
                 "time_mean": float(np.mean(times)), "time_std": float(np.std(times))}
        if wstats_rows:
            stats["w_max_over_seeds"] = float(np.max(wstats_rows))
        with open(os.path.join(RESULTS, "e5", f"e5_{tag}_stats.json"), "w") as f:
            json.dump(stats, f, indent=2)
        print(f"[e5] {tag}: mse mean={arr.mean():.4e} std={arr.std():.4e} "
              f"cv={arr.std()/arr.mean()*100:.1f}%")
    sm.COLS += ["w_mean", "w_max", "w_zero_frac", "m_max"]
    sm.write()


# ------------------------------- E6 -------------------------------
def e6():
    """E6 复杂度（两条维度，AGENT.md §7.2「改变场景三角形数量或光源数量」）：
      A. 三角形数 {1k, 10k, 100k}：cornell 网格填充，光源数固定 1；
      B. 光源数 {1, 4, 16, 64}：cornell 主面光源拆分为等面积子光源，三角形数固定。
    均为 {PT, ReSTIR 复用} spp=4，记录分类射线数与时间。
    几何变化的场景无独立 GT，因此只报告成本维度（射线/时间），不报 MSE。
    """
    sm = Summary(os.path.join(RESULTS, "e6", "e6_summary.csv"))
    cases = [(f"cornell_tri{b}", ["--tri-budget", str(b)]) for b in (1000, 10000, 100000)]
    cases += [(f"cornell_light{n}", ["--light-count", str(n)]) for n in (1, 4, 16, 64)]
    for name_base, geo in cases:
        for tag, method, extra in [
            ("pt", "pt", ["--reuse-spatial", "0"]),
            ("restir", "restir", ["--reuse-spatial", "1", "--biased", "0"]),
        ]:
            name = f"{name_base}_{tag}"
            out = os.path.join(RESULTS, "e6", name)
            # 网格填充场景无独立 GT（几何已变），不计算 MSE；只记录射线/时间
            j, tm, ts, io = render_timed("cornell", method, 4, out_prefix=out,
                                         extra=extra + geo,
                                         write_pfm=False)
            row = base_row(j, name, tm, ts, io, {})
            row["triangles"] = j["triangles"]
            row["light_count"] = j.get("light_count", 1)
            sm.add(row)
            print(f"[e6] {name}: tris={j['triangles']} lights={row['light_count']} "
                  f"t={tm:.3f}s rays={j['total_rays']} rays/px={j['rays_per_pixel']:.2f}")
    sm.COLS = [c for c in sm.COLS if c not in ("mse_linear", "psnr_linear",
                                               "mse_tonemap", "psnr_tonemap")]
    sm.COLS.insert(5, "triangles")
    sm.COLS.insert(6, "light_count")
    sm.write()


# --------------------------- 偏差地板分解 ---------------------------
def biasfloor():
    """ReSTIR 是一阶间接模型（x0→x1→光），GT 为 depth-4 PT → 存在截断偏差。
    用 PT --max-depth 2 高采样渲染作为「同路径空间参考」，分解：
      总误差 MSE(restir, GT) ≈ 截断偏差 MSE(GT_d2, GT) + 模型内误差 MSE(restir, GT_d2)
    （近似分解，非严格正交）。输出 results/biasfloor/biasfloor.csv。"""
    rows = []
    for scene in SCENES:
        gt = load_gt(scene)
        ref = os.path.join(RESULTS, "biasfloor", f"{scene}_ptd2_ref")
        if not os.path.exists(ref + ".pfm"):
            print(f"[biasfloor] {scene} PT depth-2 spp={GT_SPP} ...", flush=True)
            run_render(scene, "pt", GT_SPP, GT_SEED, ref,
                       extra=["--max-depth", "2"])
        gt_d2 = read_pfm(ref + ".pfm")
        trunc = evaluate(gt_d2, gt)
        for spp in [4, 64]:
            p = os.path.join(RESULTS, "e1", f"{scene}_restir_reuse_unbiased_s{spp}.pfm")
            m_in = evaluate(read_pfm(p), gt_d2)
            rows.append({"scene": scene, "spp": spp,
                         "mse_truncation": trunc["mse_linear"],
                         "mse_restir_vs_d2ref": m_in["mse_linear"]})
            print(f"[biasfloor] {scene} spp={spp}: trunc={trunc['mse_linear']:.3e} "
                  f"within-model={m_in['mse_linear']:.3e}")
    os.makedirs(os.path.join(RESULTS, "biasfloor"), exist_ok=True)
    with open(os.path.join(RESULTS, "biasfloor", "biasfloor.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)


def list_cmds():
    print("# GT: 3 场景 × 2 次独立渲染 (pt spp=4096, 两种子；平均为参考 + 自噪声估计)")
    print("# E1: 3 场景 × 3 配置 × spp{1,4,16,64} × 3 次计时重复 = 108 渲染")
    print("# E2: 无新渲染（E1 数据双向插值）")
    print("# E3: 4 半径 × 4 候选 × 3 次 @ cornell spp=4 = 48 渲染")
    print("# E4: S2 × {pt,unbiased,biased} × spp{4,16} × 3 次 = 18 渲染")
    print("# E5: 8 种子 × {pt,restir} @ cornell spp=4 = 16 渲染")
    print("# E6: (三角形数{1k,10k,100k} + 光源数{1,4,16,64}) × {pt,restir} × 3 次 = 42 渲染")
    print("# biasfloor: 3 场景 PT depth-2 spp=4096 参考")


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "list"
    force = "--force" in sys.argv
    if which == "gt":
        gen_gt(force=force)
    elif which == "e1":
        e1()
    elif which == "e2":
        e2()
    elif which == "e3":
        e3()
    elif which == "e4":
        e4()
    elif which == "e5":
        e5()
    elif which == "e6":
        e6()
    elif which == "biasfloor":
        biasfloor()
    elif which == "all":
        e1(); e2(); e3(); e4(); e5(); e6(); biasfloor()
    else:
        list_cmds()


if __name__ == "__main__":
    main()
