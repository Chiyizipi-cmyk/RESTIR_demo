# 基于 ReSTIR GI 的离线渲染 Demo —— 最终报告

> 课题编号 A6 ｜ 方向：全局光照与蒙特卡洛渲染
> 交叉引用：选型 `docs/00-selection.md` ｜ 设计 `docs/01-design.md` ｜ 实验数据 `docs/02-performance.md` ｜ 进展与回溯 `docs/progress.md` ｜ 原始数据 `results/`
> 测试环境：AMD Ryzen 7 7840H（8C/16T），MSYS2 GCC 16.1.0 Release（`-O3`），16 线程；
> 全部结果同命令同种子逐位可复现（1 线程与 16 线程输出 SHA256 相同）。

---

## 1. 引言与相关工作

蒙特卡洛路径追踪（PT）在低采样率下噪声严重，收敛速度仅为 $O(1/\sqrt{N})$。
Resampled Importance Sampling（RIS）通过"从提议分布抽候选、按目标函数权重重采样"
把昂贵的采样转化为廉价的候选生成，成为近十年实时/离线渲染降噪的核心工具。
本项目实现一个**简化版 ReSTIR GI 离线渲染器**：在相邻像素之间通过 Reservoir Sampling
复用间接光照路径，定量研究空间复用对噪声、偏差、漏光与稳定性的影响。

**ReSTIR 谱系**（本项目的技术脉络）：

1. **RIS（Talbot 2005）**：首次将重要性重采样引入全局光照，用 $M$ 个廉价候选逼近
   难采样的目标分布 $\hat p$，估计 $\langle L\rangle = f(y)/\hat p(y)\cdot(1/M)\sum w_i$。
2. **WRS / ReSTIR DI（Bitterli 2020）**：把 RIS 流式化为 Weighted Reservoir Sampling
   （$O(1)$ 内存单遍更新），输出权重 $W = w_{sum}/(M\cdot\hat p(y))$，
   并给出 reservoir 间合并公式。
3. **ReSTIR GI（Ouyang 2021）**：本项目直接蓝本。把 reservoir 从直接光推广到间接路径，
   提出 **reconnection shift**：复用邻居路径时保持间接顶点 $x_1'$ 不变，
   仅把前缀重连到当前像素首命中点 $x_0$。
4. **GRIS（Lin 2022）**：给出 ReSTIR 的一般化数学基础，明确 shift mapping 的 Jacobian
   项与无偏条件；本项目据此标注 reconnect-to-vertex（Jacobian = 1）的适用边界。

此外参考 Wyman 2023《A Gentle Introduction to ReSTIR》作为实现主线。
本项目边界（详见 `docs/00-selection.md` §3）：Lambertian 材质、**单面发光的矩形面光源**、
一阶间接路径（$x_0\to x_1\to$ 光源）、纯空间复用（无时域）、CPU 离线渲染。

## 2. 方法

完整数学推导见 `docs/01-design.md` §2–3，此处提炼要点。

### 2.1 间接光照的 RIS 估计

对像素 $q$，间接路径 $z=(x_0,x_1,l)$ 的目标函数取贡献的亮度：

$$\Phi_q(z)=f_r(x_0)\,G(x_0,x_1)\,V(x_0,x_1)\cdot f_r(x_1)\,G(x_1,l)\,V(x_1,l)\,L_e(l),
\qquad \hat p_q(z)=\mathrm{luminance}(\Phi_q(z))$$

初始采样每像素生成 **1 条**候选路径（$x_0$ 余弦采样得 $x_1$，$x_1$ 处 NEE 采样光源点 $l$），
提议密度 $q(z)=p_A(x_1)$ 为面积测度 BSDF 采样 pdf；光源采样的 $1/p_A(l)$ 已含于 NEE
估计（suffix）内，**不重复计入 $q$**。单样本 WRS 更新：$w_{sum}\mathrel{+}=w_i$、
$M\mathrel{+}=1$、以 $w_i/w_{sum}$ 概率替换 $y$。

样本的 suffix 取

$$\text{suffix}(x_1)=\underbrace{f_r(x_1)G(x_1,l)V(x_1,l)L_e(l)/p_A(l)}_{\text{NEE}(x_1)}$$

即"$x_1$ 处反射出射辐亮度"的无偏估计。**不含 $x_1$ 自身的自发光**：
若 $x_1$ 落在光源上，该方向属于 $x_0$ 的**直接光照**，已由 $x_0$ 的 NEE 覆盖，
计入 suffix 会重复计数。

### 2.2 空间复用与 Reconnection Shift

从半径 $r$ 的屏幕邻域均匀选 $k$ 个邻居，对每个邻居 reservoir $r_i$ 执行重连
$T(z')=(x_0,x_1',l')$，重估 $f_r(x_0)$、$G(x_0,x_1')$、$V(x_0,x_1')$ 得
$\hat p_q(T(z'))$，再以候选权重 $\hat p_q(T(z'))\cdot W_{r_i}\cdot M_{r_i}$ 合并入当前
reservoir；最终

$$W_c=\frac{w_{sum}}{M_c\cdot\hat p_q(c.y)}$$

**候选计数不变量**：即使邻居 reservoir 无效（$W_{r}=0$ 或 $\hat p_q=0$），其 $M_r$ 也必须
并入 $M_c$（只是 $w=0$）。否则 $W$ 会虚高，图像整体偏亮（开发期实测偏亮 38 %，见 §3）。

**Jacobian**：Lambertian 场景下 $x_1'$ 恒为表面真实顶点，重连映射在面积乘积测度下是恒等嵌入，
$J=1$（GRIS 无偏条件在此简化下成立）。

### 2.3 Biased / Unbiased 双模式

- **Unbiased**（`--biased=0`）：重连时发射阴影射线检测 $V(x_0,x_1')$，不可见则
  $\hat p_q=0$（候选权重为 0，但仍计入 $M$）；
- **Biased**（`--biased=1`）：省略 $V(x_0,x_1')$ 检测，降低方差但允许穿墙复用（漏光）。
  光源可见性 $V(x_1',l')$ 两种模式均保留。

### 2.4 spp 语义

ReSTIR 模式下 `--spp` 仅缩放**直接光照** NEE 采样数；间接光始终为每像素 1 条初始候选
（Ouyang 2021 §3.1 无 temporal 的离线口径），间接方差由空间复用抑制。
PT 模式下 `--spp` 为常规每像素路径数。

### 2.5 光照模型与"仅 NEE"的采样方案

场景使用**单面发光的矩形面光源**：几何法线由顶点绕序决定，且必须指向被照亮的空间。
直接光照只由逐顶点 NEE 估计；BSDF 采样射线命中发光面时**不再计入自发光**
（除相机主射线可直接看到光源本身外），否则会与 NEE 重复计数。
本项目不实现 NEE/BSDF 之间的 MIS——这是一个有意的简化，其代价是失去
"BSDF 采样命中光源"这一低方差路径，但在小面积光源 + 漫反射设定下 NEE 已是高效估计器。

## 3. 实现要点

**工程结构**（C++20 / CMake，第三方库仅 GLM + tinyexr + stb，全部 vendored）：

- `src/renderer/`：NEE-PT 基线与 ReSTIR 渲染器（G-Buffer → Initial Sampling →
  Spatial Reuse → Shade 四阶段），两者**共享场景/求交/材质/光源采样代码路径**，
  保证对比公平；
- `src/restir/`：`reservoir.h`（WRS 单样本更新 + merge）、`shift.cpp`
  （reconnection shift + $\hat p$ 求值）；
- `src/scene/`：自写分桶 SAH BVH + 程序化场景 S1–S3（选型阶段放弃 Embree，
  理由见 `docs/00-selection.md` §2；CMake 预留 `USE_EMBREE` 升级路径）；
- `tests/`：8 个测试套件（WRS 概率分布、merge 公式、无遮挡重连、漏光可见性行为、
  **直接光照/NEE 朝向**、BVH、RNG、渲染冒烟 + 确定性）。

**确定性并行**：像素按静态行块划分到线程，每像素使用独立 PCG32 流
（`seed ⊕ 像素索引`），无线程间共享累加 → 1 线程与 16 线程输出 PFM 逐位一致
（SHA256 相同）。这保证"同命令同种子逐位复现"，也使计时口径在多线程下可比。

**射线分类统计**：全程按 primary / gi / shadow_nee / shadow_reuse 四类原子计数，
随 JSON 配置输出，是 E2/E6 射线预算口径的数据来源。

### 3.1 缺陷修复记录（`docs/progress.md` 阶段 2 与阶段 5）

**阶段 2（开发期）**

1. **提议密度 $q$ 重复计入光源 pdf（间接光偏暗）**：suffix 内部 NEE 已含 $1/p_A(l)$，
   `pass_initial` 中 $q$ 再乘一次导致间接光偏暗 $p_A(l)$ 倍。
2. **合并跳过失效邻居的 $M$ 计数（偏亮 38 %）**：`pass_spatial_reuse` 原先对
   $W=0$ 或 $\hat p_q=0$ 的邻居直接 `continue`，其 $M$ 未并入 $M_c$，导致 $W$ 虚高。

**阶段 5（交付评审期，两处均为影响核心结论的缺陷）**

3. **面光源法线朝向错误 → NEE 静默失效**：光源四边形的顶点绕序使其几何法线指向
   **背离房间**的一侧，而 `light_contribution` 用 $\cos\theta_l=\max(0,-\mathbf n\cdot\omega_i)$
   做单面发光判定，于是**所有 NEE 贡献恒为 0**。症状是"基线收敛极慢、
   图像大面积为 0"，被误读为"低采样率下的真实统计特性"。修复为按房间内侧绕序
   重建光源四边形（`add_ceiling_light_quad`），并将"单面发光"判定统一到
   NEE 与 BSDF 命中两条代码路径（`Hit::front`）。
4. **BSDF 命中发光面与 NEE 重复计数**：NEE 修复后，`trace_path` 仍对 BSDF 采样命中的
   发光面追加自发光，而该方向上的辐射等价于上一顶点的直接光照，已由 NEE 覆盖。
   实测使图像**偏亮约 40 %**（S1 depth-4 spp=64 图像均值 0.254 vs 收敛值 0.181）。
   修复为直接光仅由 NEE 估计（§2.5），ReSTIR 的 suffix 同步去掉 $L_e(x_1)$。

缺陷 3、4 共同说明：**一个失效的 NEE 会同时伪装成"基线噪声大"与"ReSTIR 增益高"**。
修复后新增回归测试 `tests/test_lighting.cpp`（朝向约定 + NEE/BSDF 双估计量交叉验证
+ "仅开 NEE 时房间必须被照亮"的渲染级断言），确保同类缺陷不会再静默通过。

## 4. 实验结果

GT 为三场景 NEE-PT `spp=4096`、两种子平均（等效 8192 spp，自噪声 ≤6.3e-5，
比被测量小 2 个数量级以上）；MSE 在线性 HDR 空间计算。
完整数值见 `docs/02-performance.md`，图表位于 `results/figs/`。

### 4.1 E1 收敛对比与 E2 等预算对比

三场景 × {PT(NEE), ReSTIR 无复用, ReSTIR 空间复用} × spp {1,4,16,64}，每配置 4 种子。

| 场景 | spp | PT MSE | ReSTIR 复用 MSE | 等射线预算改善倍数 |
|---|---|---|---|---|
| S1 Cornell | 1 | 4.96e-3 | **1.77e-3** | — |
| S1 Cornell | 4 | 1.25e-3 | 1.60e-3 | **×2.21** |
| S1 Cornell | 16 | 3.15e-4 | **1.33e-3** | ×1.19 |
| S1 Cornell | 64 | **7.87e-5** | 1.24e-3 | ×0.39 |
| S2 Occlusion | 4 | 5.41e-5 | **7.66e-5** | **×2.14** |
| S3 HDR | 1 | 5.79e-1 | **4.29e-1** | ×1.26 |
| S3 HDR | 4 | 1.30e-1 | **1.19e-1** | **×3.08** |
| S3 HDR | 16 | 3.27e-2 | 4.37e-2 | **×3.58** |

结论：

- **PT 基线正确且稳定**（4 种子 CV 3–7 %，MSE 随 spp 单调下降）；
- **ReSTIR 无复用**的 MSE 不随 spp 改善（间接候选恒为 1 条），三场景分别稳定在
  3.4e-3 / 1.4e-4 / 1.2e-1——**增益全部来自空间复用**；
- **等 spp 口径**下 ReSTIR 只在低预算（spp≤4）领先（S1 ×2.8、S3 ×1.35），
  spp≥16 后 PT 反超：ReSTIR 的间接候选数不随 spp 增长，这是方法固有的自由度差异；
- **等射线预算**是 ReSTIR 的真正优势口径：S3 全区间 ×1.26–3.58，S1/S2 中低预算 ×2.1–2.2，
  而每像素射线数仅为 PT 的 1/6–1/4；
- **等时间口径**结论弱于射线口径：128² 下墙钟时间仅 6–24 ms，线程启动与 4 个图像级
  pass 的固定开销与射线计算同量级，S1/S2 的"改善倍数 <1"主要来自固定开销。

视觉对比见 `results/figs/quad_{cornell,occlusion,hdr}.png`。

### 4.2 E3 复用参数扫描

S1、spp=4、4 种子：**候选数是主导参数**（c=1→8 使 MSE 降低约 1.7–1.8×，所有半径下单调）；
**半径 1–5 影响很弱**（同候选数下差异 ≤12 %，多在种子间 std 内），r=1–2 略优。
时间几乎不随参数变化（0.008–0.011 s）。单种子版本曾得出"半径 5 明显变差"的结论，
在 4 种子统计下不成立——那是 RIS 随机波动而非参数效应。图 `e3_heatmap.png`。

### 4.3 E4 偏差与漏光

漏光采用**配对指标**：biased 与 unbiased 共享同一邻居选择随机流，唯一差别是
省略 $V(x_0,x_1')$，故 $\Delta=\mathrm{lum}(\text{biased})-\mathrm{lum}(\text{unbiased})$
只反映漏光（旧版 `|biased−GT|` 阈值实际测到的是噪声）。

| 半径 | 漏光像素比例（GT 暗区） | 暗区漏光密度 | 整体 MSE（biased / unbiased） |
|---|---|---|---|
| 2 | 0.085 % | 4.01e-5 | 8.29e-5 / 8.30e-5 |
| 5 | 0.140 % | 7.26e-5 | 8.87e-5 / 8.76e-5 |
| 10 | 0.299 % | 1.21e-4 | 1.05e-4 / 1.03e-4 |

（spp=16）漏光随复用半径**单调增长**，验证了成因；但绝对量级很小，
因为重连目标函数中的几何项 $G(x_0,x_1')$ 天然抑制了大部分穿墙复用。
整体 MSE 差异 <1 %，说明在本简化设定下 biased 模式**几乎不引入可测偏差**，
结论不能外推到镜面/薄结构场景。图 `e4_compare_s{4,16}.png`、`e4_leak_radius.png`。

### 4.4 E5 稳定性

| 方法 | MSE 均值 | 标准差 | CV | $w_{\max}$ |
|---|---|---|---|---|
| PT (NEE) | 1.241e-3 | 2.17e-5 | **1.7 %** | — |
| ReSTIR 复用 | 1.749e-3 | 9.82e-4 | **56.2 %** | **4001.8** |

8 个种子中 7 个 MSE ∈ [1.29e-3, 1.53e-3]，**种子 41 出现 MSE=4.34e-3 的 firefly**
（$w_{\max}=4001.8$）。触发机制是 **NEE 距离奇点**：初始候选的 $x_1$ 非常靠近面光源时
$G(x_1,l)\propto1/d^2$ 使 $w_{sum}$ 出现极端值，单像素异常经空间复用扩散到邻域。
这是 RIS 权重的重尾性质，非实现缺陷；评价必须同时给出均值与中位数。

### 4.5 E6 复杂度

- **三角形数** {1k, 10k, 100k}：两者时间近似线性增长（ReSTIR 恒为 PT 的约 1/3），
  每像素射线数与场景规模无关（PT ≈19、ReSTIR ≈5.5–6.2）。
- **面光源数** {1, 4, 16, 64}（等面积拆分、总功率不变）：射线数完全不变，
  时间无单调趋势——线性 CDF 扫描在 ≤64 光源下开销可忽略。

## 5. 讨论：偏差、漏光与稳定性

### 5.1 偏差

| 来源 | 量级 | 证据 |
|---|---|---|
| 路径深度截断（一阶间接，无 ≥3 次反弹） | S1 3.79e-4，S2 4.90e-5，S3 3.37e-4 | `results/biasfloor/biasfloor.csv` |
| biased 模式省略 $V(x_0,x_1)$ | 整体 MSE 差异 <1 %，暗区漏光 ≤0.3 % 像素 | E4 |
| 有限邻域空间相关性 | 半径 1–5 内 ≤12 %（多为噪声） | E3 |
| Jacobian = 1 近似 | 恒成立（Lambertian 限定） | `docs/01-design.md` §2.5 |

Biasfloor 分解表明**主要误差是 RIS 估计方差而非模型截断**：S1 在 s4 的模型内误差
1.44e-3 ≫ 截断 3.79e-4；S3 更极端（1.14e-1 ≫ 3.37e-4）。即继续加深路径对当前误差
贡献很小，方差抑制才是瓶颈。

### 5.2 漏光

见 §4.3。要点：漏光随复用半径单调增长（受控验证），但绝对量级小
（r=10 时 0.3 % 暗区像素），原因是几何项的自然抑制。因此在本简化设定下
**biased 模式是性价比很高的选择**（省掉全部 `shadow_reuse` 射线，偏差不可测）。

### 5.3 稳定性

见 §4.4。ReSTIR 的 CV 是 PT 的 33 倍；单个离群种子即可把 8 种子均值抬高 3 倍以上。
这也说明为什么本报告同时给出均值与中位数、E1/E3 采用 4 种子：
**在重尾估计器上，单次运行的 MSE 不具代表性**。

### 5.4 局限性与未来工作

**局限**：

1. 仅 Lambertian 材质与一阶间接路径，reconnect-to-vertex 的 $J=1$ 结论不能推广到
   镜面/多次反弹；
2. 无时域复用，静态场景下每像素重做初始采样，候选利用率低；
3. 未实现 NEE/BSDF 的 MIS，直接光完全依赖 NEE（小光源下高效，大光源下非最优）；
4. 未对 firefly 做抑制（$W$ 有界化、NEE 最近距离限制），高预算下均值指标仍受重尾影响；
5. 分辨率固定 128²，时间口径被固定开销主导，结论向高分辨率迁移未验证。

**未来工作**：按 GRIS 框架实现一般化 shift（含镜面半向量映射与非平凡 Jacobian）；
引入时域复用提升有效 $M$；对 $W$ 做有界化或改用 ratio estimator 抑制重尾；
实现 NEE/BSDF 的 MIS；在真实 glTF 场景（Embree/更大规模）上复测 E6。

## 6. 结论

本项目从零实现了 NEE-PT 基线与简化版 ReSTIR GI（WRS + reconnection shift +
biased/unbiased 双模式），完成 E1–E6 全部实验矩阵，并修复了两处影响核心结论的实现缺陷
（光源朝向导致 NEE 静默失效、BSDF 命中与 NEE 重复计数）。核心结论：

1. **空间复用的价值在低预算与等射线预算下最明确**：等射线口径下 S3 全区间领先
   PT 1.26–3.58 倍，S1/S2 中低预算领先约 2.1–2.2 倍，每像素射线数仅为 PT 的 1/6–1/4；
   等 spp 口径下 spp≥16 后 PT 反超。
2. **候选数是比邻域半径更有效的参数**（×1.7–1.8 vs ≤12 %）。
3. **biased 模式在本设定下偏差不可测**（整体 MSE 差异 <1 %，漏光 ≤0.3 % 像素），
   代价是重尾方差——这是它最需要被评价纪律覆盖的部分。
4. **主要误差来源是 RIS 估计方差，而非一阶间接模型的截断偏差**（后者小约 1 个数量级）。

全部结果可通过 `tools/run_experiments.py` 与 `tools/make_plots.py` 一键复现，
同命令同种子逐位一致。

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
