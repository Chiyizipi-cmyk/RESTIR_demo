# 项目进展记录（AENGT.md §10.5 中途汇报）

## 阶段 0：调研与选型（tag v0-selection）

**完成**：
- 文献调研：ReSTIR 谱系（Talbot 2005 RIS → Bitterli 2020 WRS/ReSTIR DI → Ouyang 2021 ReSTIR GI → Lin 2022 GRIS）。
- 选型：C++20 + CMake + 纯 CPU 实现；GLM 数学库；无其他第三方依赖。
- 交付 `docs/00-selection.md`（选题论证、文献对比表、可行性分析）。

**与计划偏差**：无。

---

## 阶段 1：设计（tag v1-design）

**完成**：
- `docs/01-design.md`：渲染方程、RIS/WRS 公式推导、重连接 shift（Jacobian=1 简化）、三阶段流水线（G-Buffer → Initial → Reuse → Shade）数据流图、CLI 参数设计、实验矩阵 E1–E6 设计、确定性可复现方案（静态行块 + 每像素独立 PCG32 流）。

**与计划偏差**：无。

---

## 阶段 2：实现（tag v2-impl）

**完成**：
- 核心四模块：路径生成（`renderer/pt_renderer.cpp` / `restir_renderer.cpp`）、重连接（`restir/shift.cpp`）、可见性检测（`scene/scene.cpp` 可见性查询 + unbiased/biased 开关）、Reservoir 权重更新（`restir/reservoir.h`）。
- 基线 NEE-PT 渲染器；三测试场景（S1 Cornell / S2 Occlusion / S3 HDR Small Light）；cornell 支持 `--tri-budget` 网格盒子填充。
- CLI 开关齐全：`--method/--reuse-spatial/--biased/--spatial-radius/--candidates-per-pixel/--spp/--seed/--max-depth/--tri-budget`。
- 单元测试：四模块各配测试（`tests/`，ctest 全过）。
- 逐位可复现验证：1 vs 16 线程 PFM 哈希一致。
- 射线分类统计：primary / gi / shadow_nee / shadow_reuse 四类原子计数。

**与计划偏差**：无。

---

## 阶段 3：性能评估（当前）

**完成**：
- 工具链：`tools/pfm_io.py`（PFM 读取）、`tools/metrics.py`（MSE/PSNR）、`tools/run_experiments.py`（GT + E1–E6 驱动）、`tools/make_plots.py`（全部图表）。
- GT：三场景 PT spp=4096 seed=4294967295。
- 实验矩阵全部跑完：E1（3 场景×3 配置×4 spp×4 种子）、E2（等预算插值）、E3（半径×候选扫描）、E4（biased/unbiased + 漏光掩码）、E5（8 种子稳定性）、E6（三角形数扩展性）、biasfloor（截断偏差分解）。
- 图表 12 张（`results/figs/`），写入 `docs/02-performance.md`。

**关键 bug 修复（评估过程中发现）**：
1. **q 重复计入光源 pdf（偏暗）**：`pass_initial` 中初始候选权重 q 误含 `ls.pdf`；suffix 内部 NEE 已含 `1/p_A(l)`，重复计入导致间接光偏暗 p_A(l) 倍。修复后 64 种子均值 0.1550±0.0036 ≈ depth-2 参考 0.1548。
2. **合并跳过失效 reservoir 的 M（偏亮 38%）**：`pass_spatial_reuse` 原跳过无效邻居（W=0/p̂=0），导致其 M 未并入 M_c、W 虚高。修复为失效候选也调用 `reservoir_merge`（p̂_q=0 但 M 累加），修复后 64 种子均值 0.1518±0.0036 ✓。
3. **文档同步**：`docs/01-design.md` §3.1 的 q(z) 公式与 spp 语义已更新为最终实现口径。

**与计划偏差**：
- 原计划"spp 同时缩放间接候选数"改为"spp 仅缩放直接光照、间接每像素 1 候选"（Ouyang 2021 §3.1 无 temporal 的离线口径），避免 w_sum 虚高。
- E1 由单种子改为 4 种子统计（均值/中位数/std），以抑制 RIS 重尾 firefly 对均值的支配；图表实线用中位数、虚线用均值。
- E2 由"固定预算插值 ReSTIR"改为"以 ReSTIR 各 spp 为锚在 PT 曲线插值"（ReSTIR 时间/射线范围窄于 PT，原方向插值越界）。

**下一步**：阶段 4 总结报告 `docs/03-report.md` + 交付清单核对 + tag v4-final。
