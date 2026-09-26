# AGENT.md —— 基于 ReSTIR GI 的离线渲染 Demo（课题编号 A6）

> 本文档是 Agent 执行本项目的唯一行动纲领。任何阶段开始执行前，先完整阅读本文档；每个阶段的产出物必须满足本文档定义的验收标准后才能进入下一阶段。

---

## 1. 项目概述

- **课题名称**：基于 ReSTIR GI 的离线渲染 Demo
- **课题编号**：A6（仅限单人完成）
- **方向标签**：全局光照与蒙特卡洛渲染
- **一句话定义**：实现一个简化版 ReSTIR GI 离线渲染器，通过 Reservoir Sampling 在相邻像素之间复用间接光照路径，提高低采样率下的渲染质量。
- **硬性约束**：
- **不要求实时运行**，重点在于正确性、可复现性与性能数据的严谨性，而非帧率。
- 核心目标是**理解**路径重采样、空间复用及其对渲染噪声的改善，代码必须反映这一理解，禁止"黑盒式"拼接现成渲染器。

## 2. 目标与交付物映射

课题目标（来自任务书）→ 必须落地的交付物：

| 目标维度 | 任务书要求 | 对应交付物 | 验收标准 |
| --- | --- | --- | --- |
| 技术 | 理解路径追踪、重要性采样、蒙特卡洛估计、Reservoir Sampling 与空间复用 | 设计文档第 3–4 章（原理推导与算法设计），代码注释 | 能推导出 RIS / WRS / 空间复用的权重更新公式，文档含完整数学表述 |
| 工程 | 完成路径生成、重连接、可见性检测和 Reservoir 权重更新的可运行渲染器 | Demo 工程（可编译、可运行、可复现实验） | 一键构建通过；命令行可复现全部实验；核心四模块各有单元测试 |
| 研究 | 使用 MSE、PSNR、射线数和运行时间评价效果，并分析偏差、漏光与稳定性问题 | 性能数据报告（图表 + 分析章节） | 至少覆盖 §7.2 规定的全部实验矩阵；每项结论有数据支撑 |

## 3. 阶段划分（总体流程）

项目按以下五个阶段顺序执行，**每阶段完成并通过检查清单后方可进入下一阶段**。阶段间允许有限回溯（如发现设计缺陷），但回溯必须在阶段总结中记录。

```text
阶段 0  调研与选型（技术路线调研，形成选型决策记录）
阶段 1  设计（完成基本设计文档 v1.0）
阶段 2  实现（完成 Demo 工程，含测试）
阶段 3  性能评估（跑齐实验矩阵，产出性能数据）
阶段 4  总结（最终报告、文档定稿、交付清单核对）
```

---

## 4. 阶段 0：调研与选型

### 4.1 调研范围

1. **核心理论**（必读，按序阅读）：

- Talbot et al. 2005, *Importance Resampling for Global Illumination*（RIS 基础）
- Bitterli et al. 2020, *Spatiotemporal Reservoir Resampling for Real-Time Ray Tracing with Dynamic Direct Lighting*（ReSTIR 原始框架，SIGGRAPH 2020）
- Ouyang et al. 2021, *ReSTIR GI: Path Resampling for Real-Time Path Tracing*（HPG 2021，本课题的直接蓝本）
- Lin et al. 2022, *Generalized Resampled Importance Sampling: Foundations of ReSTIR*（GRIS，数学基础，理解 reconnection shift 与无偏条件）
- Wyman et al. 2023, *A Gentle Introduction to ReSTIR*（SIGGRAPH course notes，入门主线）

2. **对照方法**：经典 Path Tracing（PT）、带 next-event estimation 的 PT（NEE-PT），作为性能对比基线。
3. **工程实现参考**：NVIDIA RTXDI 文档、PBRT 第 4 版（pbr-book.org）相关章节。

### 4.2 选型决策项（每项必须给出结论 + 理由 + 备选方案）

| 决策项 | 候选 | 决策要求 |
| --- | --- | --- |
| 场景与几何格式 | glTF（tinygltf）/ 其他 | 推荐 glTF + tinygltf：轻量、生态成熟；如选 Assimp 须说明动机 |
| 射线求交加速 | Intel Embree / 自写 BVH | 推荐 Embree（课题技术栈指定）；CPU 端、C++ API 成熟 |
| 数学库 | GLM | 指定项，直接使用 |
| 图像 I/O | OpenEXR / tinyexr | HDR 输出必须支持；分析阶段可再转 PNG 对比 |
| 并行化方案 | std::thread / TBB / OpenMP | 需调研并选型，说明对计时口径的影响 |
| 随机数 | PCG / 其他 | 要求**可复现**：固定种子能逐位复现同一渲染结果 |
| 实验与绘图 | Python + NumPy + Matplotlib | 指定项；性能分析脚本独立成目录 |

### 4.3 阶段产出

- `docs/00-selection.md`：选型决策记录（每项决策：选项、结论、理由、风险）。

### 4.4 检查清单

- [ ] 4 篇核心文献均阅读并在文档中有对应笔记/要点摘录
- [ ] 所有选型决策项均有明确结论
- [ ] 明确本项目"简化版"的边界：支持哪些材质（建议起步：Lambertian 漫反射；镜面可选）、哪些光源（建议：点光源 + 环境光 + 可选面光源）、路径深度上限、是否含时域复用（建议：以**纯空间复用**为主，时域复用作为可选扩展）
- [ ] 可复现性方案（固定种子策略）已定义

---

## 5. 阶段 1：设计

### 5.1 产出

`docs/01-design.md`（基本设计文档），必须包含以下章节，缺一不可：

1. **总体架构**：模块划分图与数据流（场景加载 → G-Buffer/首命中 → 路径生成 → 初始采样（Initial Sampling）→ Reservoir 构建 → 空间复用（Spatial Reuse）→ 着色输出）。
2. **数学基础**：蒙特卡洛渲染方程、重要性采样、RIS 流程与无偏性条件、Weighted Reservoir Sampling（WRS）算法及权重递推式、目标分布 $p̂$ 的选择（间接光照贡献近似）、reservoir 数据结构（y, w_sum, M, W）。
3. **核心算法设计**：

- 初始采样：每像素生成 1 条（或少量）间接路径，计算其目标函数权重；
- **重连接（Reconnection Shift）**：邻居像素路径复用到当前像素时的顶点重连策略、Jacobian 项处理（简化版可按 Ouyang et al. 2021 对漫反射场景的 reconnect-to-vertex 处理并明确其偏差来源）；
- **可见性检测**：复用路径接入当前像素时的阴影射线（visibility ray）处理，及其对无偏性的影响（Biased vs. Unbiased 两种模式的开关设计）；
- **Reservoir 权重更新**：空间复用阶段的候选合并（Merge）与权重 $W$ 重计算公式；
- **空间复用策略**：邻域形状（如 5×5 屏幕空间邻域）、每像素候选数（如 5）、偏差/方差权衡（biased 模式的归一化）。

4. **数据结构**：Reservoir、路径（Path/Vertex）存储、像素缓冲区的内存布局；明确路径在复用时需要保留哪些信息（顶点位置、法线、BSDF、吞吐量、原始采样 pdf、jacobian 等）。
5. **接口设计**：核心类/函数签名（C++），包括求交、采样、reservoir 更新、复用、输出。
6. **测试场景设计**：至少 3 个场景——

- S1：Cornell Box（漫反射，含间接光主导区域）；
- S2：几何遮挡复杂场景（用于暴露**漏光**问题，如多房间/多隔板结构）；
- S3：高动态范围小光源场景（用于暴露采样难度与偏差）。

7. **实验设计**：预设实验矩阵（见 §7.2），确定真值参考图（Ground Truth）的生成方案——用高采样数（如 ≥ 4096 spp）的 NEE-PT 渲染作为 GT，并明确 GT 本身噪声对指标的影响。
8. **风险分析**：偏差来源（缺失可见性、目标函数失配、reconnection 近似）、漏光成因、稳定性（reservoir 退化、权重爆炸、异常值像素）。

### 5.2 检查清单

- [ ] 文档中公式编号完整，WRS 权重递推式可对照论文验证
- [ ] reconnection shift 的数学处理有明确推导或论文出处，并标注本项目采用的简化及其后果
- [ ] biased / unbiased 两种模式的设计意图与开关方式明确
- [ ] 三个测试场景的描述足以让实现阶段直接建模
- [ ] 设计文档通过"走查"：从输入命令到输出图像的完整流程可被逐模块追溯

---

## 6. 阶段 2：实现

### 6.1 工程结构（建议，可按实际调整但需在文档中说明）

```text
restir-gi/
├── CMakeLists.txt
├── README.md                 # 构建、运行、复现实验说明
├── third_party/              # Embree / GLM / tinygltf / tinyexr 等
├── src/
│   ├── core/                 # 数学、随机数、相机、射线类型
│   ├── scene/                # 场景加载、材质、光源、Embree 场景封装
│   ├── renderer/             # PathTracer（基线）、ReSTIRRenderer、GBuffer
│   ├── restir/               # Reservoir、InitialSampling、SpatialReuse、ShiftMapping
│   └── io/                   # EXR/PNG 输出
├── tests/                    # 单元测试（Catch2 或自研断言框架）
├── scenes/                   # 内置测试场景（程序化生成或 glTF 文件）
├── tools/                    # Python 分析脚本（指标计算、绘图）
└── results/                  # 实验原始数据与图像（Git 可忽略大数据）
```

### 6.2 实现要求

1. **先基线，后 ReSTIR**：先实现可正确渲染 Cornell Box 的 NEE-PT 基线，再在其上叠加 ReSTIR GI。基线必须与 ReSTIR 共享场景/求交/材质代码路径，保证对比公平。
2. **核心四模块必须逐一实现并可独立测试**：

- 路径生成（Path Generation）
- 重连接（Reconnection Shift）
- 可见性检测（Visibility Test）
- Reservoir 权重更新（Reservoir Update）

3. **模式开关**：渲染器必须支持以下开关，全部可通过命令行配置：

- `--method={pt,restir}`、`--reuse-spatial={0,1}`、`--biased={0,1}`、`--spatial-radius`、`--candidates-per-pixel`、`--spp`、`--seed`、``--max-depth`

4. **可复现性**：同一命令 + 同一种子 → 逐位相同输出（注意浮点求和顺序在多线程下需按行/块归并或采用确定性调度，须在 README 中说明）。
5. **代码质量**：C++20；核心算法处注释引用对应论文公式编号；无内存泄漏（可用 sanitizers 抽查）；CMake 支持 Debug/Release。
6. **单元测试**至少覆盖：

- WRS 抽样概率与权重分布的正确性（大样本统计检验）；
- Reservoir merge 后 $W$ 值与论文公式一致；
- Reconnection shift 在无遮挡场景下的正确性（与直接采样对比期望一致）；
- 可见性检测对漏光场景的行为符合预期。

### 6.3 检查清单

- [ ] `README.md` 中构建命令一键通过（Linux，g++/clang++，C++20）
- [ ] 基线 NEE-PT 在 S1 上渲染 100 spp 无可见错误
- [ ] ReSTIR 开启空间复用后，同 spp 下噪声显著低于基线（定性目测 + 定量指标双重确认）
- [ ] biased / unbiased、有/无空间复用四组配置均可运行
- [ ] 全部单元测试通过
- [ ] 命令行参数与实验矩阵一一对应

---

## 7. 阶段 3：性能评估

### 7.1 评价指标（任务书指定，缺一不可）

| 指标 | 计算方式 | 说明 |
| --- | --- | --- |
| **MSE** | 相对 GT 的均方误差（在线性 HDR 空间计算，tone mapping 后再算一份作参考） | 主指标 |
| **PSNR** | 由 MSE 换算（注明峰值取值与色彩空间） | 主指标 |
| **射线数** | 渲染全程发射的各类射线总数（primary / shadow / GI 路径 / visibility 复用射线分别统计） | 必须按类别拆分统计，并在结果中给出"每像素平均射线数" |
| **运行时间** | 总墙钟时间（warm-up 后计时，重复 ≥ 3 次取均值±方差），并区分纯渲染时间与 I/O 时间 | 报告所用 CPU 型号、线程数、编译优化等级 |

### 7.2 实验矩阵（最低要求）

| 实验 | 目的 | 内容 |
| --- | --- | --- |
| E1 收敛对比 | 噪声改善 | S1/S2/S3 上，PT vs ReSTIR（无复用/有空间复用），spp ∈ {1, 4, 16, 64}，绘制 MSE-时间、MSE-射线数曲线 |
| E2 效率对比 | 同等预算对比 | 固定时间预算与固定射线预算两种口径，对比两方法 MSE |
| E3 复用参数扫描 | 权衡分析 | 空间半径 ∈ {1,2,3,5} × 每像素候选数 ∈ {1,3,5,8}，观察质量-成本曲线与拐点 |
| E4 偏差分析 | 偏差来源验证 | biased vs unbiased 模式在同一遮挡场景（S2）的 MSE 与视觉差异；漏光现象的定位与量化（如受影响像素比例） |
| E5 稳定性 | 稳健性 | 多种子（≥ 8 个）重复渲染，统计 MSE/时间的均值与方差；权重异常值（W 过大、reservoir 退化）监测 |
| E6 复杂度 | 规模敏感性 | 改变场景三角形数量或光源数量，观察射线/时间增长趋势 |

### 7.3 产出

- `results/`：原始图像（EXR + PNG）、原始数据（CSV/JSON，含完整运行配置）
- `docs/02-performance.md`（或并入最终报告）：每项实验的图表 + 结论
- 图表要求：MSE/PSNR-时间曲线、MSE-射线数曲线、参数扫描热力图、四宫格对比图（同 spp 下 PT vs ReSTIR、局部放大）

### 7.4 检查清单

- [ ] GT 生成方式与噪声水平已说明
- [ ] 7.1 四项指标全部有数据；射线数按类别拆分
- [ ] 7.2 六项实验全部完成，缺项必须有书面理由
- [ ] 每项实验结论均有对应图表支撑，图表编号可在正文中索引
- [ ] 环境信息（CPU/编译器/线程数）完整记录

---

## 8. 阶段 4：总结

### 8.1 产出

1. **最终报告** `docs/03-report.md`（建议 15–25 页量级），结构：

- 引言与相关工作（ReSTIR 谱系：RIS → ReSTIR DI → ReSTIR GI → GRIS）
- 方法（提炼自设计文档）
- 实现要点（工程取舍：并行化、内存布局、Embree 使用）
- 实验结果（提炼自性能数据，全部引用 `results/` 图表）
- 讨论：**偏差、漏光与稳定性**的专项分析（任务书明确要求，必须独立成节，包含失败案例图像）
- 局限性与未来工作
- 参考文献

2. **交付清单核对**（见 §9）
3. 各文档定稿：选型记录、设计文档、性能报告、最终报告交叉引用一致。

### 8.2 检查清单

- [ ] 报告中每个性能结论可回溯到 `results/` 中的原始数据
- [ ] 偏差/漏光/稳定性三节均有现象描述、成因分析、数据或图像证据
- [ ] 参考文献格式统一（建议 BibTeX 风格条目）

---

## 9. 最终交付物清单（Definition of Done）

| # | 交付物 | 位置 | 完成判定 |
| --- | --- | --- | --- |
| 1 | 基本设计文档 | `docs/01-design.md` | 含 §5.1 全部 8 章，通过检查清单 |
| 2 | Demo 工程 | 仓库根目录 | 一键构建通过，四组配置可运行，单测全过 |
| 3 | 性能数据 | `results/` + `docs/02-performance.md` | 实验矩阵 E1–E6 完成，指标齐全 |
| 4 | 选型决策记录 | `docs/00-selection.md` | 全部决策项有结论与理由 |
| 5 | 最终报告 | `docs/03-report.md` | 结构完整，偏差/漏光/稳定性分析到位 |
| 6 | README | `README.md` | 陌生人可凭此复现全部实验 |

---

## 10. 工程规范（全程适用）

1. **版本管理**：Git 管理，按阶段打 tag：`v0-selection`、`v1-design`、`v2-impl`、`v3-eval`、`v4-final`。提交信息遵循 `阶段: 简述` 格式。
2. **文档与代码同步**：接口变更必须同步更新设计文档；实验配置变更必须同步更新实验矩阵说明。
3. **禁止事项**：

- 禁止直接使用现成 ReSTIR 实现（如 RTXDI SDK 的 ReSTIR 模块）充当本项目实现——只允许参考其文档；
- 禁止伪造或手填实验数据——所有数字必须来自脚本输出并可重跑验证；
- 禁止跳过基线渲染器直接实现 ReSTIR（对比公平性是研究结论成立的前提）。

4. **数据口径纪律**：所有时间/射线统计必须注明配置；跨实验对比必须控制变量（同一场景、同一 GT、同一 spp 口径）。
5. **中途汇报**：每阶段结束时输出阶段小结（完成了什么、与计划偏差、下一步），写入 `docs/progress.md`。

## 11. 参考资料（必读清单与出处）

1. B. Bitterli, C. Wyman, M. Pharr, P. Shirley, A. Lefohn, W. Jarosz. *Spatiotemporal Reservoir Resampling for Real-Time Ray Tracing with Dynamic Direct Lighting*. ACM TOG 39(4), SIGGRAPH 2020.
2. Y. Ouyang, S. Liu, M. Kettunen, M. Pharr, J. Pantaleoni. *ReSTIR GI: Path Resampling for Real-Time Path Tracing*. Computer Graphics Forum 40(8), HPG 2021, pp. 17–29. —— **本项目直接蓝本**
3. D. Lin, M. Kettunen, B. Bitterli, J. Pantaleoni, C. Yuksel, C. Wyman. *Generalized Resampled Importance Sampling: Foundations of ReSTIR*. ACM TOG 41(4), SIGGRAPH 2022.
4. C. Wyman, et al. *A Gentle Introduction to ReSTIR*. SIGGRAPH 2023 Course Notes.
5. J. Talbot, D. Cline, P. Egbert. *Importance Resampling for Global Illumination*. EGSR 2005.
6. M. Pharr, W. Jakob, G. Humphreys. *Physically Based Rendering, 4th Edition*. https://pbr-book.org/4ed/contents
7. NVIDIA RTXDI 文档（工程参考，非实现来源）。