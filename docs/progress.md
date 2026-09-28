# 项目进展记录（AENGT.md §10.5 中途汇报）

> 六个阶段顺序执行：0 调研选型 → 1 设计 → 2 实现 → 3 性能评估 → 4 总结 →
> **5 交付评审与返工**（阶段 5 为评审后新增；阶段 3/4 的部分结论被其修正，
> 差异在阶段 5 中逐条记录）。

## 阶段 0：调研与选型（tag v0-selection）

**完成**：
- 文献调研：ReSTIR 谱系（Talbot 2005 RIS → Bitterli 2020 WRS/ReSTIR DI →
  Ouyang 2021 ReSTIR GI → Lin 2022 GRIS），Wyman 2023 作为实现主线。
- 选型：C++20 + CMake + 纯 CPU；GLM；自写分桶 SAH BVH（放弃 Embree 并给出理由，
  预留 `USE_EMBREE`）；程序化场景（放弃 glTF/tinygltf 并给出理由）；
  tinyexr + stb + 自研 PFM；`std::thread` 静态行块；PCG32 每像素独立流。
- 交付 `docs/00-selection.md`。

**与计划偏差**：无。

---

## 阶段 1：设计（tag v1-design）

**完成**：`docs/01-design.md`：渲染方程、RIS/WRS 公式推导、重连接 shift
（Jacobian = 1 简化）、四阶段流水线数据流图、数据结构与接口、CLI 参数设计、
实验矩阵 E1–E6、GT 与自噪声方案、确定性可复现方案、风险分析。

**与计划偏差**：无。

---

## 阶段 2：实现（tag v2-impl）

**完成**：
- 核心四模块：路径生成、重连接（`restir/shift.cpp`）、可见性检测（biased/unbiased 开关）、
  Reservoir 权重更新（`restir/reservoir.h`）。
- 基线 NEE-PT 渲染器；三测试场景（S1 Cornell / S2 Occlusion / S3 HDR）；
  cornell 支持 `--tri-budget` 网格填充。
- CLI 开关齐全：`--method/--reuse-spatial/--biased/--spatial-radius/`
  `--candidates-per-pixel/--spp/--seed/--max-depth/--tri-budget`。
- 单元测试（自研断言框架）+ 逐位可复现验证（1 vs 16 线程 PFM 哈希一致）。
- 射线分类统计：primary / gi / shadow_nee / shadow_reuse。

**开发期修复的 bug**：
1. **q 重复计入光源 pdf（间接光偏暗）**：`pass_initial` 中初始候选权重 q 误含
   `ls.pdf`；suffix 内部 NEE 已含 1/p_A(l)，重复计入导致间接光偏暗 p_A(l) 倍。
2. **合并跳过失效 reservoir 的 M（偏亮 38 %）**：`pass_spatial_reuse` 原对
   W=0 或 p̂=0 的邻居直接跳过，其 M 未并入 M_c、W 虚高。修复为失效候选也调用
   `reservoir_merge`（w=0 但 M 累加）。

**与计划偏差**：无。

---

## 阶段 3：性能评估（tag v3-eval）

**完成**：
- 工具链：`tools/pfm_io.py`、`tools/metrics.py`、`tools/run_experiments.py`、
  `tools/make_plots.py`。
- 实验矩阵全部跑完：E1–E6 + biasfloor + GT 自噪声；图表写入 `results/figs/`。

**与计划偏差**：
- 原计划"spp 同时缩放间接候选数"改为"spp 仅缩放直接光、间接每像素 1 候选"
  （Ouyang 2021 §3.1 无 temporal 的离线口径）。
- E1/E3 由单种子改为多种子统计（RIS 重尾下单次运行不具代表性）。
- E2 由"固定预算插值 ReSTIR"改为"以 ReSTIR 各 spp 为锚在 PT 曲线插值"。

> 本阶段产出的结论在阶段 5 被**大幅修正**（NEE 失效导致基线被削弱）。
> 现存的 `docs/02-performance.md` 已是阶段 5 重新生成的版本。

---

## 阶段 4：总结（tag v4-final）

**完成**：`docs/03-report.md` 初稿、README、交付清单核对。

> 本阶段报告中的核心数值结论已在阶段 5 重新生成并重写。

---

## 阶段 5：交付评审与返工（tag v5-fixed）

### 5.1 评审发现的问题

| 编号 | 问题 | 严重度 |
|---|---|---|
| P0-1 | 面光源几何法线背离房间，捕获率 cos_l 恒为 0 → **NEE 完全失效**，基线退化为纯 BSDF 采样 | 致命 |
| P0-2 | NEE 生效后暴露：BSDF 命中发光面又加一次自发光 → **与 NEE 重复计数，偏亮约 40 %** | 致命 |
| P1-1 | E5 离群种子编号写错（文档写 seed 53，数据实为 seed 67/41） | 事实错误 |
| P1-2 | E1 结论"PT 单调下降、均值≈中位数、CV 低"与自身数据矛盾 | 分析错误 |
| P1-3 | 漏光指标用 abs(biased-GT) 绝对阈值，实际测到的是 firefly 噪声而非漏光 | 方法错误 |
| P1-4 | E3 单种子，无法分辨半径效应（结论不可靠） | 方法缺陷 |
| P1-5 | E6 未做设计文档承诺的"光源数量"维度 | 覆盖缺口 |
| P1-6 | 设计文档承诺报告 GT 自噪声，最终报告缺失 | 覆盖缺口 |
| P1-7 | 设计文档与实现的 PathSample / 接口签名 / CLI 列表不一致 | 文档漂移 |
| P1-8 | 工作区 README.md 被删除（git 中仍存在） | 交付物缺失 |

### 5.2 修复内容

**代码**

1. 新增 `triangle_normal()` 与 `Hit::geo_n`、`Hit::front`；新增
   `add_ceiling_light_quad()` / `add_ceiling_light_grid()`，按**房间内侧绕序**
   重建 S1/S2/S3 的全部面光源（法线朝下）。
2. `lighting.cpp`：光源法线改用 `triangle_normal()`，单面发光判定显式短路。
3. `pathtracer.cpp` / `restir_renderer.cpp`：BSDF（含初始候选）命中发光面时
   不再计入自发光——直接光只由 NEE 估计；`Hit::front` 统一两条路径的朝向口径。
4. `restir_renderer.cpp`：suffix 改为纯 NEE(x1)，x1 落在光源上的初始候选直接丢弃。
5. 新增 `--light-count`（cornell 主光源等面积拆分，E6 光源维度）。
6. 新增回归测试 `tests/test_lighting.cpp`（朝向约定 / NEE 与 BSDF 双估计量交叉验证 /
   反例 / "仅开 NEE 时房间必须被照亮"的渲染级断言）；`tests/test_reconnection.cpp`
   的 q 与渲染器口径对齐。

**实验工具**

7. GT 改为两种子独立渲染后取平均，并输出 `results/gt/gt_self.json`（P1-6）。
8. E3 改为 4 种子统计（P1-4）。
9. E4 改为**配对漏光指标**（biased − unbiased，限定 GT 暗区）+ 复用半径扫描
   {2,5,10}（P1-3，并补强漏光成因的受控验证）。
10. E6 增加"面光源数量 {1,4,16,64}"维度（P1-5）。

**文档**：`docs/02-performance.md`、`docs/03-report.md` 按新数据全文重写；
`docs/01-design.md` 同步数据结构/接口/E6/GT/漏光指标；`README.md` 同步。

### 5.3 修复前后的核心结论对比

| 指标 | 修复前（阶段 4） | 修复后（阶段 5） |
|---|---|---|
| 基线含义 | 名义 NEE-PT，实际为纯 BSDF 采样 | 真正的 NEE-PT（逐顶点直接光采样） |
| S1 PT spp=4 MSE | 1.71e-1（中位 1.42e-1） | 1.25e-3（降低约 100 倍） |
| S1 PT 种子间 CV | 22 % | 1.7 % |
| ReSTIR 复用 spp=1 vs PT | 领先 5.1 倍 | 领先 2.8 倍（方向不变，幅度更小） |
| ReSTIR 复用 spp=64 | 均值反超 PT 36 倍 | 落后 PT 16 倍，中位数不再异常 |
| S2 漏光像素 | 0.31 %（实为噪声） | 0.085 %（真漏光，随半径 0.085→0.299 % 单调） |
| E5 ReSTIR CV | 113 % | 56 %（离群种子 1/8） |

### 5.4 验证

- `restir_tests`：8 套件 / 200060 checks 全过（含新增 NEE 回归）；
- 确定性：1 vs 16 线程 PFM SHA256 相同；
- "仅开 NEE（`--max-depth 1`）时房间被照亮"：修复前近零像素占比 97.8 %，
  修复后 40.8 %（其余为相机视野外与光源自身像素）；
- S1 depth-4 收敛值 0.1805（64 spp，单种子）≈ 1024 spp 参考 0.1806，
  与修复前 4096 spp 的 GT 均值 0.185 相差 2.5 % 以内 → 修复未改变收敛目标，
  只恢复了收敛速度。

**项目状态**：修复后 E1–E6 全矩阵重跑、图表与文档全部重新生成；
tag 序列 v0-selection → v1-design → v2-impl → v3-eval → v4-final → v5-fixed。
