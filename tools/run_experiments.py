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
from metrics import evaluate
from pfm_io import read_pfm

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "restir-gi.exe")
RESULTS = os.path.join(ROOT, "results")
GT_DIR = os.path.join(RESULTS, "gt")

WIDTH = HEIGHT = 128
GT_SPP = 4096
GT_SEED = 4294967295
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
def gen_gt():
    os.makedirs(GT_DIR, exist_ok=True)
    for scene in SCENES:
        out = gt_path(scene)
        if os.path.exists(out + ".pfm"):
            print(f"[gt] 已存在，跳过 {scene}")
            continue
        print(f"[gt] {scene} spp={GT_SPP} ...", flush=True)
        j = run_render(scene, "pt", GT_SPP, GT_SEED, out, write_exr=True)
        print(f"[gt] {scene}: {j['time_render_sec']:.1f}s, rays={j['total_rays']}")


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
    """E3 参数扫描（S1 cornell, spp=4, unbiased 复用）：半径{1,2,3,5}×候选{1,3,5,8}。"""
    sm = Summary(os.path.join(RESULTS, "e3", "e3_summary.csv"))
    gt = load_gt("cornell")
    for radius in [1, 2, 3, 5]:
        for cand in [1, 3, 5, 8]:
            name = f"cornell_r{radius}_c{cand}"
            out = os.path.join(RESULTS, "e3", name)
            extra = ["--reuse-spatial", "1", "--biased", "0",
                     "--spatial-radius", radius, "--candidates-per-pixel", cand]
            j, tm, ts, io = render_timed("cornell", "restir", 4, out_prefix=out, extra=extra)
            m = measure(out, gt)
            sm.add(base_row(j, name, tm, ts, io, m))
            print(f"[e3] {name}: mse={m['mse_linear']:.3e} t={tm:.3f}s rays={j['total_rays']}")
    sm.write()


# ------------------------------- E4 -------------------------------
def e4():
    """E4 偏差分析（S2 occlusion, spp=4/16）：
    PT 基线 vs ReSTIR unbiased 复用 vs ReSTIR biased 复用。
    漏光量化：GT 暗（luminance<0.05）但 biased 渲染偏亮（>GT+0.25）的像素比例。"""
    sm = Summary(os.path.join(RESULTS, "e4", "e4_summary.csv"))
    gt = load_gt("occlusion")
    for spp in [4, 16]:
        for tag, method, extra in [
            ("pt", "pt", ["--reuse-spatial", "0"]),
            ("unbiased", "restir", ["--reuse-spatial", "1", "--biased", "0"]),
            ("biased", "restir", ["--reuse-spatial", "1", "--biased", "1"]),
        ]:
            name = f"occlusion_{tag}_s{spp}"
            out = os.path.join(RESULTS, "e4", name)
            j, tm, ts, io = render_timed("occlusion", method, spp, out_prefix=out, extra=extra)
            m = measure(out, gt)
            if tag == "biased":
                img = read_pfm(out + ".pfm")
                lum_gt = 0.2126 * gt[..., 0] + 0.7152 * gt[..., 1] + 0.0722 * gt[..., 2]
                lum_im = 0.2126 * img[..., 0] + 0.7152 * img[..., 1] + 0.0722 * img[..., 2]
                leak = (lum_gt < 0.05) & (lum_im > lum_gt + 0.25)
                m["leak_pixel_ratio"] = float(np.mean(leak))
                m["leak_pixel_count"] = int(np.sum(leak))
                np.save(os.path.join(RESULTS, "e4", f"leakmask_s{spp}.npy"), leak)
            sm.add(base_row(j, name, tm, ts, io, m))
            print(f"[e4] {name}: mse={m['mse_linear']:.3e} t={tm:.3f}s"
                  + (f" leak={m['leak_pixel_ratio']*100:.1f}%" if "leak_pixel_ratio" in m else ""))
    sm.COLS += ["leak_pixel_ratio", "leak_pixel_count"]
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
    """E6 复杂度：cornell --tri-budget {1000,10000,100000}，PT spp=4 + ReSTIR spp=4。"""
    sm = Summary(os.path.join(RESULTS, "e6", "e6_summary.csv"))
    for budget in [1000, 10000, 100000]:
        for tag, method, extra in [
            ("pt", "pt", ["--reuse-spatial", "0"]),
            ("restir", "restir", ["--reuse-spatial", "1", "--biased", "0"]),
        ]:
            name = f"cornell_t{budget}_{tag}"
            out = os.path.join(RESULTS, "e6", name)
            # 网格填充场景无独立 GT（几何已变），不计算 MSE；只记录射线/时间
            j, tm, ts, io = render_timed("cornell", method, 4, out_prefix=out,
                                         extra=extra + ["--tri-budget", budget],
                                         write_pfm=False)
            row = base_row(j, name, tm, ts, io, {})
            row["triangles"] = j["triangles"]
            sm.add(row)
            print(f"[e6] {name}: tris={j['triangles']} t={tm:.3f}s rays={j['total_rays']}")
    sm.COLS = [c for c in sm.COLS if c not in ("mse_linear", "psnr_linear",
                                               "mse_tonemap", "psnr_tonemap")]
    sm.COLS.insert(5, "triangles")
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
    print("# GT: 3 场景 × 1 次 (pt spp=4096)")
    print("# E1: 3 场景 × 3 配置 × spp{1,4,16,64} × 3 次计时重复 = 108 渲染")
    print("# E2: 无新渲染（E1 数据双向插值）")
    print("# E3: 4 半径 × 4 候选 × 3 次 @ cornell spp=4 = 48 渲染")
    print("# E4: S2 × {pt,unbiased,biased} × spp{4,16} × 3 次 = 18 渲染")
    print("# E5: 8 种子 × {pt,restir} @ cornell spp=4 = 16 渲染")
    print("# E6: tri-budget{1k,10k,100k} × {pt,restir} × 3 次 = 18 渲染")
    print("# biasfloor: 3 场景 PT depth-2 spp=4096 参考")


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "list"
    if which == "gt":
        gen_gt()
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
