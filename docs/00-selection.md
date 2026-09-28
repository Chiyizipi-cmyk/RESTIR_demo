# 阶段 0：调研与选型决策记录

> 课题 A6 —— 基于 ReSTIR GI 的离线渲染 Demo
> 本文档记录全部选型决策：每项给出「选项 / 结论 / 理由 / 风险」。

---

## 1. 核心理论调研笔记

按 AGENT.md §4.1 指定顺序阅读，以下为各文献要点摘录及其对本项目的直接指导意义。

### 1.1 Talbot et al. 2005, *Importance Resampling for Global Illumination*（RIS 基础）

- 核心思想：对难以直接采样的目标分布 $\hat p$，先从易采样的提议分布 $q$ 抽取 $M$ 个候选 $\{x_1,\dots,x_M\}$，为每个候选赋权重 $w_i = \hat p(x_i)/q(x_i)$，再以 $w_i/\sum_j w_j$ 的概率重采样出一个样本 $y$。
- 重采样后的样本近似服从 $\hat p$ 归一化后的分布；估计量 $\langle L\rangle = \frac{f(y)}{\hat p(y)}\cdot\frac{1}{M}\sum_j w_j$ 是无偏的（当 $q$ 的支撑覆盖 $\hat p$）。
- 对本项目的意义：RIS 是 ReSTIR 的数学内核。本项目 initial sampling 阶段每像素只取 $M=1$（ReSTIR GI 论文的默认配置），重采样的威力主要来自**空间复用阶段对邻居 reservoir 的合并**，而非单像素内的多候选 RIS。

### 1.2 Bitterli et al. 2020, *Spatiotemporal Reservoir Resampling*（ReSTIR 原始框架）

- 提出用 **Weighted Reservoir Sampling（WRS）** 把 RIS 流化：reservoir 结构 $(y, w_{sum}, M, W)$，支持 O(1) 单样本更新与 reservoir 间合并（merge），无需存储全部候选。
- 关键公式：单样本更新 $w_{sum} \leftarrow w_{sum} + w_i$，以 $w_i/w_{sum}$ 概率替换 $y$；选中后 $W = \frac{w_{sum}}{M\cdot\hat p(y)}$。
- 合并公式：把输入 reservoir $r$ 的样本作为候选并入当前 reservoir，候选权重为 $\hat p_q(r.y)\cdot W_r \cdot M_r$（在当前像素上下文重估目标函数），$M$ 相加。
- 偏差讨论：空间复用中若忽略可见性（visibility）项，则估计有偏但方差显著降低；论文提出 biased（省可见性测试）与 unbiased（完整可见性测试）两种模式——本项目将两者都实现为运行期开关（`--biased={0,1}`）。
- 时域复用（temporal reuse）依赖帧间重投影，本项目为**静态离线渲染**，不实现时域复用（见 §3 简化边界）。

### 1.3 Ouyang et al. 2021, *ReSTIR GI: Path Resampling for Real-Time Path Tracing*（本项目直接蓝本）

- 把 ReSTIR 从「光源采样」推广到「路径采样」：每像素生成一条短间接路径（initial candidate），空间复用阶段把邻居像素的**路径后缀**通过 **reconnection shift** 重连到当前像素的路径前缀上。
- Reconnection shift：设当前像素路径前缀终点为 $x_0$（首命中点），邻居路径包含顶点序列 $x_0', x_1', \dots$。复用时选取邻居路径上的**重连顶点** $x_1'$，构造新路径 $x_0 \to x_1' \to \dots$（保持 $x_1'$ 之后的路径后缀不变）。
- 当重连顶点是场景表面上的**真实顶点**（而非方向采样的半向量）时，新旧路径在面积测度下的映射是恒等映射，Jacobian 行列式为 1；这是本项目采用的简化（详见 `docs/01-design.md` §3.3 的推导与偏差来源标注）。
- 目标函数 $\hat p$ 取路径对当前像素贡献的标量近似（throughput × 入射辐亮度 的亮度/luminance）。
- 论文同样提供 biased / unbiased 两种可见性处理；biased 模式会**漏光**（light leaking）——穿墙复用邻居的光照，这正是本项目实验 E4 要量化分析的现象。

### 1.4 Lin et al. 2022, *Generalized Resampled Importance Sampling (GRIS)*

- 给出 ReSTIR 的一般数学框架：shift mapping $T$ 把邻居样本映射到当前样本域，无偏条件要求目标函数比较时使用 $\hat p_q(T(x))$ 且权重中含 Jacobian 项 $|\partial T/\partial x|$ 与源目标函数 $\hat p_s(x)$。
- 无偏空间复用的权重公式：$w = \hat p_q(T(y_s))\cdot W_s \cdot M_s$，其中 $W_s$ 已包含源域的归一化；偏差模式下可省略 Jacobian/可见性以换取低方差。
- 对本项目的意义：reconnection shift 在 GRIS 框架下是恒等 shift（Jacobian=1）的特例；无偏条件、$M$ 的处理（clamping 防退化）在实现中直接对应。

### 1.5 Wyman et al. 2023, *A Gentle Introduction to ReSTIR*（SIGGRAPH course notes）

- 作为入门主线，明确了实践中的工程要点：reservoir 的紧凑存储、$M$ 上限 clamp（如 $M \le 20\sim30$ 倍初始值）防止空间复用收敛到单一候选导致的相关性噪声（"splotchy" 伪影）、邻居采样的屏幕空间半径策略。
- 本项目采纳：默认 5×5 屏幕邻域（`--spatial-radius 2`）、每像素 5 个邻居候选（`--candidates-per-pixel 5`）、$M$ clamp 作为稳定性措施（实验 E5 监测权重异常）。

### 1.6 对照方法与工程参考

- **基线**：经典 Path Tracing（PT）与带 Next-Event Estimation 的 PT（NEE-PT，PBRT v4 第 13–14 章）。本项目基线取 NEE-PT：每个路径顶点做一次直接光采样，保证与 ReSTIR GI 公平对比（同样的材质/求交/光源代码路径）。
- **工程参考**：NVIDIA RTXDI 文档（仅参考其架构分层，不使用其实现）；PBRT v4 在线版（pbr-book.org/4ed）的采样与 BSSRDF/BVH 章节。

---

## 2. 选型决策

| 决策项 | 候选 | 结论 | 理由 | 风险与缓解 |
|---|---|---|---|---|
| 场景与几何格式 | glTF+tinygltf / 程序化生成 / Assimp | **程序化生成为主，预留 glTF 加载接口** | S1–S3 三个测试场景（Cornell Box、多隔板遮挡、HDR 小光源）几何简单，程序化生成可做到逐位可复现、零外部资产依赖、参数化（E6 需改变三角形数量，程序化场景天然支持）。Assimp 体量过大，超出现需求 | glTF 接口以抽象 `SceneBuilder` 预留，若后续引入真实资产可插拔 tinygltf；本阶段不引入以避免构建复杂度 |
| 射线求交加速 | Intel Embree / 自写 BVH | **自写 BVH（SAH 分桶 + 深度优先遍历）** | ①目标环境（Windows + MSYS2 g++）无 Embree 预编译包，引入会破坏「一键构建」；②测试场景规模 ≤ 10 万三角形，BVH 求交性能充足；③自写 BVH 代码透明，符合「禁止黑盒拼接」的课题精神；④CMake 预留 `USE_EMBREE` 选项作为后续升级路径 | 大规模场景下性能低于 Embree——E6 复杂度实验在 ≤ 10 万三角形规模内完成，风险可控 |
| 数学库 | GLM | **GLM（指定项）** | 课题指定；头文件库，零构建成本；`glm::vec3/mat4` 与图形学惯例一致 | 无实质风险；以 git 浅克隆 vendored 至 `third_party/glm_src`（构建时仅引用其头文件目录） |
| 图像 I/O | OpenEXR / tinyexr | **tinyexr + stb_image_write + 自研 PFM** | tinyexr 单头文件 + miniz，满足 HDR EXR 输出硬性要求且无 OpenEXR 的重依赖；stb_image_write 单头文件输出 PNG（tone-mapped 预览）；另输出无损 float32 PFM 供 Python 分析脚本直接读取（避免在 Python 侧解析 EXR） | tinyexr 的 ZIP 压缩写路径较慢——图像 ≤ 512×512，I/O 时间单独计时（§7.1 口径），影响可隔离 |
| 并行化方案 | std::thread / TBB / OpenMP | **std::thread + 静态分块调度** | ①零外部依赖；②**确定性**：渲染按图像水平条带静态切分给固定数量线程，每个像素使用独立 PCG 流（种子 = 全局种子 ⊕ 像素坐标 ⊕ 反弹深度），结果与线程数、调度顺序**完全无关**，满足逐位可复现要求；③OpenMP 的浮点归约顺序不确定，TBB 引入外部依赖，均不利于可复现性 | 静态分块在负载不均时略低效——场景简单、像素负载均匀，影响可忽略；计时口径为「固定线程数下的墙钟时间」，README 注明 |
| 随机数 | PCG / std::mt19937 | **PCG32（每像素独立流）** | PCG32 状态小（64 bit）、统计质量好、速度快；每像素独立播种使结果与遍历顺序无关，是确定性并行的前提；mt19937 状态大（2.5 KB）且慢 | 无实质风险；单元测试用卡方检验验证均匀性 |
| 实验与绘图 | Python+NumPy+Matplotlib | **Python + NumPy + Matplotlib（指定项）** | 课题指定；`tools/` 独立成目录：`run_experiments.py`（实验矩阵驱动）、`metrics.py`（MSE/PSNR/射线数统计）、`make_plots.py`（曲线/热力图/四宫格） | MSYS2 Python 需 pacman 安装 `mingw-w64-x86_64-python-{numpy,matplotlib}`，已在 README 记录环境准备步骤 |

---

## 3. 「简化版」边界定义

| 维度 | 本项目范围 | 明确排除 |
|---|---|---|
| 材质 | Lambertian 漫反射 + 自发光（发光面片即面光源） | 镜面/ glossy BSDF、折射、参与介质 |
| 光源 | 面光源（发光三角形/四边形，NEE 采样）+ 常量环境光（可关）+ 点光源（可配） | 环境贴图 IBL、方向光 |
| 发光面朝向 | **单面发光**：几何法线（顶点绕序决定）指向被照亮空间，场景构造时保证朝下（见 `docs/01-design.md` §2.6） | 双面发光体、带厚度的灯罩 |
| 直接光估计 | **仅 NEE**（逐顶点光源采样）；BSDF 采样命中发光面不再追加自发光 | NEE/BSDF 的 MIS（本项目未实现，作为已知简化） |
| 路径深度 | `--max-depth`，默认 4（含首命中） | 无限深度俄罗斯轮盘（可配开关，默认固定深度以保证确定性） |
| 复用 | **纯空间复用**（单帧内邻域复用） | 时域复用（temporal reuse）——静态场景无帧序列，列为未来工作 |
| Reconnection shift | reconnect-to-vertex，面积测度恒等映射，Jacobian=1（漫反射下成立；偏差来源在设计文档 §8 标注） | 一般化 shift（半向量重连、镜面顶点的 manifold shift） |
| 相机 | 针孔相机，固定参数 | 景深、运动模糊 |
| 输出 | EXR（HDR 线性）+ PNG（tonemap 预览）+ PFM（分析用） | 其他格式 |

## 4. 可复现性方案（固定种子策略）

1. 全局命令行参数 `--seed <uint64>` 为唯一种子来源；
2. 每像素随机流：`pcg32(seed, pixel_index)`，其中 `pixel_index = y*width + x`；多 bounce 通过流内顺序取数保证；
3. 空间复用的邻居选择使用**独立的第二流** `pcg32(seed ^ 0x9E3779B9, pixel_index)`，与路径采样解耦；
4. 多线程为静态分块、无线程间数据共享（每线程写各自图像条带），浮点求和顺序逐像素内部固定 → **同命令同种子逐位一致**（含不同线程数）；
5. 实验矩阵所有命令与种子记录于 `results/` 的 JSON 配置文件中，可整体重跑。

## 5. 阶段 0 检查清单核对

- [x] 4 篇核心文献均阅读并有笔记（§1.1–1.5）
- [x] 所有选型决策项均有明确结论（§2 表）
- [x] 「简化版」边界已明确：Lambertian 材质；面光源 + 环境光 + 点光源；max-depth 默认 4；纯空间复用，时域复用为可选扩展（§3）
- [x] 可复现性方案已定义（§4）

---

*下一阶段：`docs/01-design.md`（基本设计文档 v1.0）。*
