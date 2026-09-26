# 阶段 1：基本设计文档（v1.0）

> 课题 A6 —— 基于 ReSTIR GI 的离线渲染 Demo
> 本文档定义总体架构、数学基础、核心算法、数据结构、接口、测试场景、实验矩阵与风险分析。

---

## 1. 总体架构

### 1.1 模块划分与数据流

```text
┌─────────────┐
│  SceneLoader │  程序化场景构建（S1/S2/S3），输出三角形 + 材质 + 光源
│ (scene/)     │
└──────┬───────┘
       │  Scene { triangles, materials, lights, bvh }
       ▼
┌─────────────┐     ┌─────────────┐
│   Camera    │────►│  G-Buffer   │  首命中：位置 x0、法线 n0、材质 id、
│  (core/)    │     │ (renderer/) │  辐射出射度（自发光）
└─────────────┘     └──────┬──────┘
                           │
                           ▼
              ┌─────────────────────────┐
              │   Initial Sampling      │  每像素生成 1 条间接路径：
              │  (restir/)              │  从 x0 采样 BSDF 方向 → x1 → 光源采样
              └───────────┬─────────────┘
                          │  Reservoir { y=(x1, light_sample), w_sum, M, W }
                          ▼
              ┌─────────────────────────┐
              │   Spatial Reuse         │  邻域 5×5 内随机选 k 个邻居，
              │  (restir/)              │  重连接（shift）+ 可见性检测 + reservoir 合并
              └───────────┬─────────────┘
                          │  最终 reservoir
                          ▼
              ┌─────────────────────────┐
              │   Shading & Output      │  直接光照 + 间接光照（reservoir 估计）
              │  (renderer/)            │  → EXR / PNG / PFM
              └─────────────────────────┘
```

### 1.2 模块职责

| 模块 | 目录 | 职责 |
|---|---|---|
| 数学/随机数 | `src/core/` | `vec3`（GLM 封装）、`pcg32`、`Ray`、`Camera` |
| 场景 | `src/scene/` | 三角形/材质/光源定义、BVH 加速结构、程序化场景构建 |
| 基线渲染器 | `src/renderer/` | NEE-PT（next-event estimation path tracing） |
| ReSTIR 渲染器 | `src/renderer/` + `src/restir/` | G-Buffer、Initial Sampling、Spatial Reuse、Reservoir |
| 图像输出 | `src/io/` | EXR（tinyexr）、PNG（stb）、PFM（分析接口） |
| 入口 | `src/main.cpp` | CLI 解析、实验配置、计时与射线统计输出 |
| 测试 | `tests/` | WRS、merge、reconnection、visibility 单元测试 |
| 实验 | `tools/` | Python 驱动实验矩阵、指标计算、绘图 |

---

## 2. 数学基础

### 2.1 蒙特卡洛渲染方程

表面点 $x$ 沿方向 $\omega_o$ 的出射辐亮度满足

$$L(x,\omega_o) = L_e(x,\omega_o) + \int_{\Omega} f_r(x,\omega_i,\omega_o)\,L_i(x,\omega_i)\,|\cos\theta_i|\,\mathrm{d}\omega_i$$

蒙特卡洛估计（$N$ 个样本）：

$$\langle L\rangle = \frac{1}{N}\sum_{i=1}^{N} \frac{f_r(x,\omega_i,\omega_o)\,L_i(x,\omega_i)\,|\cos\theta_i|}{p(\omega_i)}$$

无偏条件：$p(\omega_i)>0$ 在被积函数非零处。

### 2.2 重要性采样与 RIS

目标分布 $\hat p(x)$ 难以直接采样时，从提议分布 $q(x)$ 抽 $M$ 个候选 $x_1,\dots,x_M$，权重

$$w_i = \frac{\hat p(x_i)}{q(x_i)}$$

以概率 $\pi_i = w_i/\sum_j w_j$ 重采样出 $y$。则

$$\langle L\rangle_{\text{RIS}} = \frac{f(y)}{\hat p(y)}\cdot\frac{1}{M}\sum_{i=1}^{M} w_i$$

是 $L$ 的无偏估计（要求 $q$ 的支撑覆盖 $\hat p$）。

### 2.3 Weighted Reservoir Sampling（WRS）

Reservoir 结构：

```cpp
struct Reservoir {
    Sample y;      // 当前选中的样本（间接路径）
    float  w_sum;  // 累计权重 Σw_i
    int    M;      // 已见候选数（可 clamp）
    float  W;      // 输出权重 = w_sum / (M * p̂(y))
};
```

**单样本更新**（流式 RIS）：

$$w_{sum} \leftarrow w_{sum} + w_i,\quad M \leftarrow M+1$$
$$y \leftarrow x_i \quad \text{以概率 } \frac{w_i}{w_{sum}}$$

**合并**（reservoir $r$ 并入当前 reservoir $c$）：

$$w_{sum} \leftarrow w_{sum} + \hat p_q(r.y)\cdot W_r \cdot M_r,\quad M \leftarrow M + M_r$$
$$c.y \leftarrow r.y \quad \text{以概率 } \frac{\hat p_q(r.y)\cdot W_r\cdot M_r}{w_{sum}}$$

**输出权重**：

$$W = \frac{w_{sum}}{M\cdot \hat p(y)}$$

> 公式对应 Bitterli 2020 §3.2–3.3 与 Lin 2022 §4。

### 2.4 目标函数 $\hat p$ 的选择

对像素 $q$ 的间接光照路径 $z = (x_0, x_1, l)$（$x_0$ 首命中点，$x_1$ 第一个间接顶点，$l$ 光源采样点），定义贡献

$$\Phi_q(z) = f_r(x_0, \omega_{x_0\to x_1}, \omega_o)\,G(x_0,x_1)\,V(x_0,x_1)\cdot f_r(x_1,\omega_{x_1\to x_0},\omega_{x_1\to l})\,G(x_1,l)\,V(x_1,l)\,L_e(l)$$

其中 $G$ 为几何项，$V$ 为可见性（0/1）。取标量目标函数

$$\hat p_q(z) = \mathrm{luminance}\big(\Phi_q(z)\big)$$

**简化说明**：本实现中，unbiased 模式计算完整 $V(x_0,x_1)$ 与 $V(x_1,l)$；biased 模式省略 $V(x_0,x_1)$（仅保留 $V(x_1,l)$ 或全部省略，由 `--biased` 控制），从而引入漏光但降低方差。

### 2.5 Reconnection Shift 的数学处理

设邻居像素 $q'$ 的路径 $z' = (x_0', x_1', l')$。重连接到当前像素 $q$ 的前缀 $x_0$：

$$T(z') = (x_0,\ x_1',\ l')$$

即保持 $x_1'$ 与 $l'$ 不变，仅把路径前缀从 $x_0'$ 换成 $x_0$。

**Jacobian**：当 $x_1'$ 是场景表面真实顶点（面积测度）时，映射 $T$ 在面积乘积测度下是恒等嵌入，Jacobian 行列式

$$J = \left|\frac{\partial T}{\partial z'}\right| = 1$$

**偏差来源标注**（对应 AGENT.md §5.1 第 3 条）：
1. 若 $x_1'$ 由方向采样产生（如镜面半向量），面积测度不成立，$J\neq 1$；本项目仅支持 Lambertian，$x_1'$ 恒为表面顶点，故 $J=1$ 成立；
2. biased 模式省略 $V(x_0,x_1)$，导致穿墙复用 → 漏光；
3. 有限邻居数（5×5 邻域、k 候选）引入空间相关性，非独立同分布，MSE 收敛速度低于理论 $1/N$。

---

## 3. 核心算法设计

### 3.1 初始采样（Initial Sampling）

每像素执行一次：

1. 从相机发射 primary ray，求交得 $x_0$（G-Buffer 阶段完成）；
2. 在 $x_0$ 处按余弦加权半球采样方向 $\omega_1$，求交得 $x_1$；
3. 在 $x_1$ 处做 NEE：从光源采样点 $l$，计算直接光照 $L_{\text{dir}}(x_1)$；
4. 构造样本 $z=(x_0,x_1,l)$，计算 $\hat p_q(z)$ 与提议 pdf $q(z)=p_{\text{BSDF}}(\omega_1)\cdot \cos\theta_1/\|x_1-x_0\|^2$（面积测度 $p_A(x_1)$）。注意：$l$ 作为 suffix 的随机部分已包含 $1/p_{\text{light}}(l)$ 的 NEE 估计，$q$ 中不再重复计入光源 pdf，否则间接光会偏暗 $p_A(l)$ 倍；
5. 写入 reservoir：$y=z,\ w_{sum}=\hat p/q,\ M=1,\ W=w_{sum}/(M\cdot\hat p(y))$。

> **spp 语义**：在 ReSTIR 渲染器中，`--spp` 仅缩放直接光照（NEE）采样数；间接路径始终为每像素 1 条初始候选（Ouyang 2021 §3.1 的离线设定，无 temporal 累积）。空间复用负责降低间接光方差。

> 对应 Ouyang 2021 §3.1「Initial Candidates」。

### 3.2 重连接（Reconnection Shift）

空间复用时，对当前像素 $q$ 与邻居 reservoir $r$：

1. 取 $r.y = (x_0', x_1', l')$；
2. 构造新路径 $z_q = (x_0, x_1', l')$；
3. 计算新方向 $\omega_{new} = \mathrm{normalize}(x_1' - x_0)$；
4. 重估 BSDF $f_r(x_0,\omega_{new},\omega_o)$、几何项 $G(x_0,x_1')$、可见性 $V(x_0,x_1')$；
5. 计算 $\hat p_q(z_q)$。

**Jacobian 处理**：因 $x_1'$ 为表面顶点且映射为恒等嵌入，$J=1$（见 §2.5）。代码中不实现一般化 Jacobian，但在注释中标注对应 GRIS 论文的公式编号。

### 3.3 可见性检测（Visibility Test）

**Unbiased 模式**（`--biased=0`）：
- 重连接时发射 shadow ray $x_0 \leftrightarrow x_1'$，检测 $V(x_0,x_1')$；
- 光源连接保留 $V(x_1',l')$（在 initial sampling 已计算）；
- 若 $V(x_0,x_1')=0$，则 $\hat p_q(z_q)=0$，该候选被丢弃。

**Biased 模式**（`--biased=1`）：
- 省略 $V(x_0,x_1)$ 检测，直接复用邻居路径；
- 仍保留 $V(x_1',l')$（避免光源穿墙）；
- 结果：方差降低，但遮挡边界处出现漏光。

### 3.4 Reservoir 权重更新（Spatial Reuse）

对当前像素 $q$：

1. 初始化空 reservoir $c$（或从 initial sampling 的 reservoir 开始）；
2. 从屏幕空间半径 $r$ 内随机选 $k$ 个邻居像素 $q_1,\dots,q_k$；
3. 对每个邻居 reservoir $r_i$：
   - 重连接得 $z_q^{(i)} = T(r_i.y)$；
   - 计算候选权重 $w_i = \hat p_q(z_q^{(i)})\cdot W_{r_i}\cdot M_{r_i}$；
   - 以概率 $w_i/w_{sum}$ 更新 $c.y$；
4. 合并完成后：$W_c = w_{sum}/(M_c\cdot\hat p_q(c.y))$。

**M clamp**：为防止 reservoir 退化（$M$ 无限增长导致新候选权重趋零），实现时可选 clamp $M \le M_{\max}$（默认 30）。

### 3.5 空间复用策略

| 参数 | 默认值 | CLI 参数 |
|---|---|---|
| 邻域形状 | 正方形 $[-r,r]^2$ | `--spatial-radius`（默认 2，即 5×5） |
| 每像素候选数 | 5 | `--candidates-per-pixel` |
| 邻居选择 | 均匀随机（独立流） | — |
| 自身 reservoir | 参与合并（$M$ 计入） | — |
| 迭代轮数 | 1 | 预留 `--spatial-iterations`（默认 1） |

**偏差/方差权衡**：
- 半径越大、候选越多，方差越低，但偏差越大（非局部信息混入）；
- biased 模式进一步降低方差，代价是漏光；
- 实验 E3 系统扫描半径 × 候选数。

---

## 4. 数据结构

### 4.1 Reservoir 与路径样本

```cpp
// restir/reservoir.h
struct PathSample {
    glm::vec3 x0;        // 首命中点（当前像素固定）
    glm::vec3 n0;        // x0 处法线
    glm::vec3 x1;        // 间接顶点（复用时共享）
    glm::vec3 n1;        // x1 处法线
    glm::vec3 light_p;   // 光源采样点
    glm::vec3 light_n;   // 光源采样点法线
    glm::vec3 Le;        // 光源辐射出射度
    int       light_id;  // 光源索引（-1 = 环境光）
    float     pdf_bsdf;  // p(ω1) at x0
    float     pdf_light; // p(l) at x1
    float     visibility_x1; // V(x0,x1) 缓存（initial 时计算）
};

struct Reservoir {
    PathSample y;
    float w_sum = 0.f;
    int   M     = 0;
    float W     = 0.f;
};
```

### 4.2 内存布局

- `std::vector<Reservoir> reservoirs`，按像素行优先索引；
- G-Buffer 单独存储 `std::vector<GBufferPixel>`（位置、法线、材质 id、albedo、自发光）；
- 复用阶段只读邻居 reservoir，写当前 reservoir，无线程竞争。

---

## 5. 接口设计

### 5.1 核心类签名（C++）

```cpp
// scene/scene.h
struct Scene {
    std::vector<Triangle> triangles;
    std::vector<Material> materials;
    std::vector<Light>    lights;
    BVH bvh;
    bool intersect(const Ray& ray, Hit& hit) const;
    bool occluded(const Ray& shadow_ray, float t_max) const;
};

Scene build_cornell_box();
Scene build_occlusion_scene();   // S2
Scene build_hdr_scene();         // S3

// renderer/pathtracer.h
class PathTracer {
public:
    Image render(const Scene& scene, const Camera& cam, const RenderConfig& cfg, Stats& stats);
};

// restir/restir_renderer.h
class ReSTIRRenderer {
public:
    Image render(const Scene& scene, const Camera& cam, const RenderConfig& cfg, Stats& stats);
private:
    void generate_gbuffer(const Scene& scene, const Camera& cam);
    void initial_sampling(const Scene& scene);
    void spatial_reuse(const Scene& scene);
    void shade(const Scene& scene);
};

// restir/reservoir.h
void reservoir_update(Reservoir& r, const PathSample& s, float w, pcg32& rng);
void reservoir_merge(Reservoir& c, const Reservoir& r, float p_hat_q, pcg32& rng);
float reservoir_weight(const Reservoir& r, float p_hat_y);

// restir/shift.h
PathSample reconnect(const PathSample& neighbor, const GBufferPixel& current_gbuf);
float evaluate_p_hat(const Scene& scene, const PathSample& z, bool unbiased, Stats& stats);
```

### 5.2 CLI 参数

```text
restir-gi --scene {cornell,occlusion,hdr}
          --method {pt,restir}
          --spp <int>              默认 1
          --width <int>            默认 128
          --height <int>           默认 128
          --max-depth <int>        默认 4
          --reuse-spatial {0,1}    默认 1（仅 restir）
          --biased {0,1}           默认 1（仅 restir）
          --spatial-radius <int>   默认 2
          --candidates-per-pixel <int> 默认 5
          --seed <uint64>          默认 42
          --threads <int>          默认 0 = hardware_concurrency
          --output <prefix>        默认 results/out
```

---

## 6. 测试场景设计

| 编号 | 名称 | 几何 | 光源 | 目的 |
|---|---|---|---|---|
| S1 | Cornell Box | 5 面墙 + 2 个立方体，Lambertian | 顶部矩形面光源 | 间接光主导区域（盒内壁、天花板）验证噪声改善 |
| S2 | Occlusion Maze | 多隔板/多房间结构，单光源被遮挡 | 1 个点光源或面光源 | 暴露漏光：biased 复用穿墙路径 |
| S3 | HDR Small Light | 大房间 + 极小高亮面光源（强度 100+） | 1 个小面光源 + 弱环境光 | 暴露采样难度：目标函数尖锐，reservoir 退化 |

---

## 7. 实验设计

### 7.1 Ground Truth 生成

- 使用 NEE-PT，spp = 4096，max-depth = 4，seed 固定为 $2^{32}-1$；
- 输出 EXR + PFM；
- GT 本身噪声：在 E1 中同时报告 GT 的 self-MSE（split-half 或 8192 spp 子样本），作为指标下界。

### 7.2 实验矩阵（对应 AGENT.md §7.2）

| 实验 | 目的 | 配置 |
|---|---|---|
| E1 收敛对比 | 噪声改善 | S1/S2/S3 × {PT, ReSTIR 无复用, ReSTIR 空间复用} × spp {1,4,16,64} |
| E2 效率对比 | 同等预算 | 固定时间 10s / 固定射线数 1M，对比 MSE |
| E3 复用参数扫描 | 权衡分析 | 半径 {1,2,3,5} × 候选数 {1,3,5,8}，S1 上 spp=4 |
| E4 偏差分析 | 偏差来源 | S2 上 biased vs unbiased，MSE + 漏光像素比例 |
| E5 稳定性 | 稳健性 | 8 个种子重复，统计 MSE/时间均值方差；监测 W 异常值 |
| E6 复杂度 | 规模敏感性 | S1 变体：三角形数 {1k, 10k, 100k}，光源数 {1,4,16} |

---

## 8. 风险分析

| 风险 | 成因 | 缓解 |
|---|---|---|
| 偏差 | 省略可见性、目标函数失配、Jacobian 近似 | unbiased 模式开关；文档标注偏差来源；E4 量化 |
| 漏光 | biased 模式穿墙复用 | S2 场景专门暴露；E4 统计受影响像素比例 |
| 权重爆炸 | $\hat p(y)$ 接近 0 时 $W\to\infty$ | clamp $W$ 上限（1e6）；E5 监测异常像素数 |
| reservoir 退化 | $M$ 过大导致新候选无法替换 | clamp $M\le 30$；E5 统计替换率 |
| 空间相关性 | 邻居复用引入非独立样本 | 增大半径/降低候选数权衡；E3 扫描 |
| GT 噪声 | 4096 spp 仍有残余噪声 | 报告 GT self-MSE 作为下界；必要时提升至 8192 spp |

---

## 9. 检查清单核对

- [x] 公式编号完整：§2.1–2.3 给出渲染方程、RIS、WRS 递推式，可对照 Bitterli 2020 / Lin 2022 验证
- [x] reconnection shift 数学处理：§2.5 推导 + 偏差来源标注（Ouyang 2021 / Lin 2022）
- [x] biased / unbiased 模式设计意图与开关方式：§3.3、§3.4、CLI `--biased`
- [x] 三个测试场景描述：§6 表，可直接建模
- [x] 设计文档走查：§1.1 数据流图可逐模块追溯（Scene → G-Buffer → Initial → Reuse → Shade → Output）

---

*下一阶段：阶段 2 实现，产出可编译 Demo 工程与单元测试。*
