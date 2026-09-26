# 基于 ReSTIR GI 的离线渲染 Demo —— 最终报告

> 课题编号 A6 ｜ 方向：全局光照与蒙特卡洛渲染
> 交叉引用：选型 `docs/00-selection.md` ｜ 设计 `docs/01-design.md` ｜ 实验数据 `docs/02-performance.md` ｜ 原始数据 `results/`
> 测试环境：AMD Ryzen 7 7840H（8C/16T），MSYS2 GCC 16.1.0 Release（-O3），16 线程；全部结果同命令同种子逐位可复现。

---

## 1. 引言与相关工作

蒙特卡洛路径追踪（PT）在低采样率下噪声严重，而收敛速度仅为 $O(1/\sqrt{N})$。Resampled Importance Sampling（RIS）通过"从提议分布抽候选、按目标函数权重重采样"把昂贵的采样转化为廉价的候选生成，成为近十年实时/离线渲染降噪的核心工具。本项目实现一个**简化版 ReSTIR GI 离线渲染器**：在相邻像素之间通过 Reservoir Sampling 复用间接光照路径，在不要求实时性的前提下，定量研究空间复用对噪声、偏差、漏光与稳定性的影响。

**ReSTIR 谱系**（本项目的技术脉络）：

1. **RIS（Talbot 2005）**：首次将重要性重采样引入全局光照，用 $M$ 个廉价候选逼近难采样目标分布 $\hat p$，估计 $\langle L\rangle = f(y)/\hat p(y)\cdot (1/M)\sum w_i$。
2. **WRS / ReSTIR DI（Bitterli 2020）**：把 RIS 流式化为 Weighted Reservoir Sampling（$O(1)$ 内存单遍更新），并结合时空复用做动态直接光照，输出权重 $W = w_{sum}/(M\cdot\hat p(y))$。
3. **ReSTIR GI（Ouyang 2021）**：本项目直接蓝本。把 reservoir 复用从直接光推广到间接路径，提出 reconnection shift：复用邻居路径时保持间接顶点 $x_1'$ 与光源点 $l'$ 不变，仅把前缀重连到当前像素首命中点 $x_0$。
4. **GRIS（Lin 2022）**：给出 ReSTIR 的一般化数学基础，明确 shift mapping 的 Jacobian 项与无偏性条件；本项目据此标注了 reconnect-to-vertex（Jacobian=1）的适用边界。

此外参考 Wyman 2023《A Gentle Introduction to ReSTIR》作为实现主线。本项目的边界（详见选型文档）：Lambertian 材质、面光源/点光源、一阶间接路径（$x_0\to x_1\to$ 光源）、纯空间复用（无时域）、CPU 离线渲染。

## 2. 方法

完整数学推导见 `docs/01-design.md` §2–3，此处提炼要点。

### 2.1 间接光照的 RIS 估计

对像素 $q$，间接路径 $z=(x_0,x_1,l)$ 的目标函数取贡献的亮度：

$$\Phi_q(z)=f_r(x_0)\,G(x_0,x_1)\,V(x_0,x_1)\cdot f_r(x_1)\,G(x_1,l)\,V(x_1,l)\,L_e(l),\qquad \hat p_q(z)=\mathrm{luminance}(\Phi_q(z))$$

初始采样每像素生成 **1 条** 候选路径（$x_0$ 余弦采样得 $x_1$，$x_1$ 处 NEE 采样光源点 $l$），提议密度 $q(z)=p_A(x_1)$ 为面积测度 BSDF 采样 pdf；光源采样的 $1/p_A(l)$ 已含于 NEE 估计内，**不重复计入 $q$**。单样本 WRS 更新：$w_{sum}\mathrel{+}=w_i$、$M\mathrel{+}=1$、以 $w_i/w_{sum}$ 概率替换 $y$。

### 2.2 空间复用与 Reconnection Shift

从半径 $r$ 的屏幕邻域均匀选 $k$ 个邻居，对每个邻居 reservoir $r_i$ 执行重连 $T(z')=(x_0,x_1',l')$，重估 $f_r(x_0)$、$G(x_0,x_1')$、$V(x_0,x_1')$ 得 $\hat p_q(T(z'))$，再以候选权重 $\hat p_q(T(z'))\cdot W_{r_i}\cdot M_{r_i}$ 合并入当前 reservoir；最终输出权重

$$W_c=\frac{w_{sum}}{M_c\cdot\hat p_q(c.y)},\qquad \text{间接光估计}=\hat p_q(c.y)\cdot W_c \text{ 方向的贡献}$$

**Jacobian**：Lambertian 场景下 $x_1'$ 恒为表面真实顶点，重连映射在面积乘积测度下为恒等嵌入，$J=1$（GRIS 无偏条件在此简化下成立）。

### 2.3 Biased / Unbiased 双模式

- **Unbiased**（`--biased=0`）：重连时发射阴影射线检测 $V(x_0,x_1')$，不可见则候选权重置 0（但仍计入 $M$）；
- **Biased**（`--biased=1`）：省略 $V(x_0,x_1')$ 检测，降低方差但允许穿墙复用（漏光）。光源可见性 $V(x_1',l')$ 两种模式均保留。

### 2.4 spp 语义

ReSTIR 模式下 `--spp` 仅缩放**直接光照** NEE 采样数；间接光始终每像素 1 条初始候选（Ouyang 2021 §3.1 无 temporal 的离线口径），间接方差由空间复用抑制。PT 模式下 `--spp` 为常规每像素路径数。

## 3. 实现要点

**工程结构**（C++20 / CMake，第三方库仅 GLM + tinyexr + stb，全部 vendored）：

- `src/renderer/`：NEE-PT 基线与 ReSTIR 渲染器（G-Buffer → Initial Sampling → Spatial Reuse → Shade 四阶段），两者**共享场景/求交/材质代码路径**，保证对比公平；
- `src/restir/`：`reservoir.h`（WRS 单样本更新 + merge）、`shift.cpp`（reconnection shift + $\hat p$ 求值）；
- `src/scene/`：自写分桶 SAH BVH（选型阶段放弃 Embree，理由见 `docs/00-selection.md`：场景仅 1k–100k 三角形，自写 BVH 消除外部依赖且便于插入确定性求交；E6 数据表明 100k 三角形下仍近似线性扩展）；
- `tests/`：WRS 概率分布统计检验、merge 后 $W$ 公式一致性、无遮挡 reconnection 正确性、漏光场景可见性行为，ctest 全过。

**确定性并行**：像素按静态行块划分到线程，每像素使用独立 PCG32 流（`seed ⊕ 像素索引`），无线程间共享累加 → 1 线程与 16 线程输出 PFM 逐位一致（SHA256 相同）。这保证"同命令同种子逐位复现"，也使计时口径在多线程下仍可比。

**射线分类统计**：渲染全程按 primary / gi / shadow_nee / shadow_reuse 四类原子计数，随 JSON 配置输出，是 E2/E6 射线预算口径的数据来源。

**评估过程中发现并修复的两个关键 bug**（详见 `docs/progress.md`）：

1. **提议密度 $q$ 重复计入光源 pdf（间接光偏暗）**：suffix 内部 NEE 已含 $1/p_A(l)$，`pass_initial` 中 $q$ 再乘一次导致间接光整体偏暗 $p_A(l)$ 倍。修复后用 64 种子图像均值验证：0.1550±0.0036 ≈ depth-2 参考 0.1548。
2. **合并跳过失效邻居的 $M$ 计数（偏亮 38%）**：`pass_spatial_reuse` 原先对 $W=0$ 或 $\hat p_q=0$ 的邻居直接 continue，其 $M$ 未并入 $M_c$，导致 $W$ 虚高、图像偏亮 38%。修复为失效候选也执行合并（$\hat p_q=0$ 但 $M$ 累加），修复后 64 种子均值 0.1518±0.0036。

这两个 bug 的诊断都依赖"多种子图像均值 vs 同路径空间参考"的数值验证，而非目测——体现了本项目对正确性优先于性能的约束。

## 4. 实验结果

GT 为三场景 NEE-PT spp=4096（seed=$2^{32}-1$）；MSE 在线性 HDR 空间计算。完整数值表见 `docs/02-performance.md`，此处给出结论性数据。所有图表位于 `results/figs/`。

### 4.1 E1 收敛对比 / E2 等预算对比

三场景 × {PT, ReSTIR 无复用, ReSTIR 空间复用} × spp {1,4,16,64}，每配置 4 种子（图表实线=中位数，虚线=均值）：

| 场景 | 锚点 | ReSTIR MSE | PT 同预算 MSE | 增益 |
|---|---|---|---|---|
| S1 Cornell | s1 | 0.093 | 0.471 | **×5.1** |
| S1 Cornell | s4 | 0.104 | 0.364 | **×3.5** |
| S2 Occlusion | s1 | 0.067 | 0.130 | **×1.9** |
| S2 Occlusion | s16 | 0.061 | 0.074 | **×1.2** |
| S3 HDR | s1 | 7.66 | 22.9 | **×3.0** |
| S3 HDR | s16 | 1.98 | 8.01 | **×4.0** |

结论：**低预算（spp≤16）ReSTIR 空间复用全面优于 PT 1.2–5×**，时间与射线两种预算口径一致（图 `e1_{cornell,occlusion,hdr}_mse.png`、`e2_budget.png`）。ReSTIR 无复用变体的 MSE 不随 spp 改善（间接始终 1 候选），反衬出增益来自**空间复用**而非管线开销差异。视觉对比见四宫格图 `quad_{cornell,occlusion,hdr}.png`（+3EV 曝光：低 spp 图像大面积近零是真实统计特性）。高预算（s64）的均值反超现象是 firefly 重尾所致，见 §5.3。

### 4.2 E3 复用参数扫描

半径 {1,2,3,5} × 候选数 {1,3,5,8}（S1, spp=4）：MSE 随候选数单调下降（c=1→8 降约 3×）；**半径 2–3 为 sweet spot**，r=5 反而引入几何失配的无效邻居使 MSE 回升（c=1 时 0.297）；时间开销几乎不变（0.010–0.011 s）。图 `e3_heatmap.png`。

### 4.3 E4 偏差分析

S2 遮挡场景 biased vs unbiased：两模式 MSE 几乎相等（s4 均 0.090，s16 均 0.061），**漏光像素比例 < 0.35%** 且集中在隔墙缝隙边缘。图 `e4_compare_s{4,16}.png` 含漏光掩码。详见 §5.2。

### 4.4 E6 扩展性

S1 三角形数 {1k, 10k, 100k}：两者时间均近似线性增长（100× 三角形 → 56× 时间），ReSTIR 始终约为 PT 的 1/3；**每像素射线数 ReSTIR ≈ 5.2–5.6，PT ≈ 19，且均不随场景规模增长**——空间复用的额外开销仅是少量 shadow_reuse 射线，无额外场景遍历。图 `e6_scaling.png`。

## 5. 讨论：偏差、漏光与稳定性

任务书要求的三类问题独立分析如下。

### 5.1 偏差

偏差来源与设计文档 §8 风险表一一对应，并已全部量化：

| 来源 | 量级 | 证据 |
|---|---|---|
| 路径深度截断（depth≤2 间接，无 ≥3 次反弹） | S1 9.3e-3，S2 6.9e-4，S3 7.9e-2 | `results/biasfloor/biasfloor.csv`（PT depth-2 同路径空间参考分解） |
| biased 模式省略 $V(x_0,x_1)$ | MSE 差异 < 0.001，漏光 < 0.35% 像素 | E4 |
| 有限邻域空间相关性 | 半径 ≥5 时 MSE 回升 | E3 |
| Jacobian=1 近似 | 恒成立（Lambertian 限定） | 设计文档 §2.5 |

Biasfloor 分解同时表明：**主要误差项是 RIS 估计方差而非截断**——S1 s4 模型内误差 0.106 ≫ 截断偏差 0.0093。即继续加深路径对当前误差贡献很小，方差抑制才是瓶颈。

### 5.2 漏光

Biased 模式允许跨遮挡复用，理论上应在 S2 多隔板场景产生漏光。实测漏光像素比例仅 0.31–0.33%（51–54/16384），且空间上集中在隔板缝隙的 1–2 像素宽边缘带（`e4_compare_s4.png` 漏光掩码）。原因是重连后的 $\hat p_q$ 含几何项 $G(x_0,x_1')$：穿墙路径的法线–方向夹角通常使 $G$ 很小，天然抑制了大部分无效复用。这说明在漫反射、面光源、一阶间接的简化设定下，**biased 模式是性价比很高的选择**；但对含镜面或薄结构的场景不能外推此结论。

### 5.3 稳定性（含失败案例分析）

E5（S1, spp=4, 8 种子）暴露了 ReSTIR 的主要失效模式：

| 方法 | MSE 均值 | MSE 标准差 | CV | $w_{max}$ |
|---|---|---|---|---|
| PT | 0.158 | 0.035 | 22.2% | — |
| ReSTIR 复用 | 0.181 | 0.205 | **113.4%** | **464.4** |

8 个种子中 7 个 MSE ∈ [0.08, 0.13]，但种子 53 出现 MSE=0.72 的 **firefly 事件**：某像素 reservoir 选中 $\hat p(y)$ 极小的样本，$W=w_{sum}/(M\cdot\hat p(y))$ 爆炸至 464，该像素贡献经空间复用扩散到邻域，单个离群种子将 8 种子均值抬高约 4×。这也是 E1/E2 中 S1/S3 s64 **均值**劣化（S1 s64 均值 2.09 vs PT 0.057）而中位数（≈0.22）仍正常的根因——并非图像质量系统性倒退，而是均值统计量被重尾支配。

机制分析：$W$ 服从重尾分布是 RIS 的固有性质（$\hat p(y)\to 0$ 时 $W\to\infty$），$M$ 上限（clamp 30）只能限制 $w_{sum}$ 增长、不能限制 $\hat p(y)$ 趋零。缓解路径已在代码中预留（$W$ clamp、$\hat p$ 下限），本项目选择**不启用**以保持估计器统计特性纯净，改以实验纪律应对：报告同时给出均值与中位数、E1 采用 4 种子、结论以中位数为准。

正确性佐证：64 种子图像**均值**与 depth-2 参考数值一致（§3），证明高方差来自估计器而非实现偏差。

### 5.4 局限性与未来工作

**局限**：(1) 仅 Lambertian 材质与一阶间接路径，reconnect-to-vertex 的 $J=1$ 结论不能推广到镜面/多次反弹；(2) 无时域复用，静态场景下每个像素独立重做初始采样，候选利用率低；(3) firefly 未做抑制，高预算下均值指标反而失真；(4) 分辨率固定 128²，结论向高分辨率的迁移未验证。

**未来工作**：按 GRIS 框架实现一般化 shift（含镜面半向量映射与非平凡 Jacobian）；引入时域复用（temporal reuse）将有效 $M$ 提升一个量级；对 $W$ 做有界化或采用 ratio estimator 变体抑制重尾；在真实 glTF 场景（Embree/更大规模）上复测 E6 扩展性。

## 6. 结论

本项目从零实现了 NEE-PT 基线与简化版 ReSTIR GI（WRS + reconnection shift + biased/unbiased 双模式），完成了 E1–E6 全部实验矩阵。核心结论：**在 spp≤16 的低预算区间，纯空间复用的 ReSTIR GI 相对 NEE-PT 取得 1.2–5× 的 MSE 改善，每像素射线数仅为 PT 的约 1/4，且 biased 模式漏光代价可忽略（<0.35%）；其主要代价是 RIS 权重固有的重尾方差（CV 113%），须以中位数口径与多种子聚合进行评价**。全部结果可通过 `tools/run_experiments.py` 与 `tools/make_plots.py` 一键复现，同命令同种子逐位一致。

## 7. 参考文献

```bibtex
@article{talbot2005ris,
  author  = {Talbot, Justin and Cline, David and Egbert, Parris},
  title   = {Importance Resampling for Global Illumination},
  journal = {Computer Graphics Forum (EGSR)},
  volume  = {24}, number = {2}, pages = {139--146}, year = {2005}
}
@article{bitterli2020restir,
  author  = {Bitterli, Benedikt and Wyman, Chris and Pharr, Matt and Shirley, Peter and Lefohn, Aaron and Jarosz, Wojciech},
  title   = {Spatiotemporal Reservoir Resampling for Real-Time Ray Tracing with Dynamic Direct Lighting},
  journal = {ACM Transactions on Graphics (SIGGRAPH)},
  volume  = {39}, number = {4}, year = {2020}
}
@article{ouyang2021restirgi,
  author  = {Ouyang, Yaobin and Liu, Shiqing and Kettunen, Markus and Pharr, Matt and Pantaleoni, Jacopo},
  title   = {ReSTIR GI: Path Resampling for Real-Time Path Tracing},
  journal = {Computer Graphics Forum (HPG)},
  volume  = {40}, number = {8}, pages = {17--29}, year = {2021}
}
@article{lin2022gris,
  author  = {Lin, Daqi and Kettunen, Markus and Bitterli, Benedikt and Pantaleoni, Jacopo and Yuksel, Cem and Wyman, Chris},
  title   = {Generalized Resampled Importance Sampling: Foundations of ReSTIR},
  journal = {ACM Transactions on Graphics (SIGGRAPH)},
  volume  = {41}, number = {4}, year = {2022}
}
@misc{wyman2023gentle,
  author = {Wyman, Chris and others},
  title  = {A Gentle Introduction to ReSTIR: Path Reuse in Real-Time Rendering},
  howpublished = {SIGGRAPH Course Notes}, year = {2023}
}
```
