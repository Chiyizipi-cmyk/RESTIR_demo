# ReSTIR GI 离线渲染 Demo（课题 A6）

基于 Reservoir Sampling 的简化版 ReSTIR GI 离线渲染器：在相邻像素间复用间接光照路径，降低低采样率下的蒙特卡洛噪声。

## 构建

依赖：CMake ≥ 3.20，支持 C++20 的编译器（g++ / clang++ / MSVC）。全部第三方库已 vendored 于 `third_party/`（GLM、tinyexr+miniz、stb_image_write），**无网络依赖**。

```bash
# Linux / macOS
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Windows (MSYS2 MinGW)
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

产物：`build/restir-gi`（渲染器）、`build/restir_tests`（单元测试）。

## 运行

```bash
# NEE-PT 基线
./build/restir-gi --scene cornell --method pt --spp 64 --seed 42 --output results/pt_64

# ReSTIR GI（空间复用，unbiased）
./build/restir-gi --scene cornell --method restir --spp 64 --seed 42 \
    --reuse-spatial 1 --biased 0 --spatial-radius 2 --candidates-per-pixel 5 \
    --output results/restir_64

# 单元测试
./build/restir_tests          # 或 ctest --test-dir build
```

每次渲染输出 `<output>.exr`（HDR）、`<output>.png`（tonemap 预览）、`<output>.pfm`（分析用 float32）、`<output>.json`（完整配置 + 分类射线统计 + 计时）。

### 命令行参数

| 参数 | 默认 | 说明 |
|---|---|---|
| `--scene` | cornell | `cornell`(S1) / `occlusion`(S2) / `hdr`(S3) |
| `--method` | restir | `pt`（NEE-PT 基线）/ `restir` |
| `--spp` | 1 | 每像素样本数：PT=路径数；ReSTIR=直接光 NEE 采样数（间接恒为每像素 1 条初始候选，方差由空间复用抑制） |
| `--max-depth` | 4 | PT 最大路径深度（表面顶点数，含首命中） |
| `--reuse-spatial` | 1 | 空间复用开关 |
| `--biased` | 1 | 1=省略重连可见性（有偏/低方差）；0=完整检测（无偏） |
| `--spatial-radius` | 2 | 邻域半径（2 → 5×5） |
| `--candidates-per-pixel` | 5 | 每像素邻居候选数 |
| `--m-cap` | 30 | reservoir M 上限（防退化） |
| `--seed` | 42 | 全局随机种子 |
| `--threads` | 0 | 0 = CPU 核数 |
| `--tri-budget` | 0 | E6：cornell 场景网格填充三角形目标数 |
| `--width` `--height` | 128 | 分辨率 |
| `--output` | results/out | 输出前缀 |

## 可复现性

- 固定种子逐位复现：每像素独立 PCG32 流（`seed ⊕ 像素索引`），多线程为**静态行块划分**，线程间无共享累加 → 同命令同种子的输出**与线程数无关、逐位一致**（已验证：1 线程与 8 线程 SHA256 相同）。
- 所有实验由 `tools/run_experiments.py` 驱动，配置与结果（JSON/CSV）落在 `results/`，可整体重跑：

```bash
python tools/run_experiments.py        # 运行全部实验矩阵 E1–E6（含 GT 生成）
python tools/make_plots.py             # 由 results/*.csv 生成全部图表
```

Python 依赖：NumPy、Matplotlib（MSYS2：`pacman -S mingw-w64-x86_64-python-{numpy,matplotlib}`）。

## 工程结构

```
├── CMakeLists.txt
├── docs/            # 00-selection / 01-design / 02-performance / 03-report / progress
├── src/
│   ├── core/        # PCG32、相机、采样工具
│   ├── scene/       # 场景、BVH（分桶 SAH）、程序化场景 S1–S3
│   ├── renderer/    # NEE-PT 基线、ReSTIR 渲染器、光源采样
│   ├── restir/      # Reservoir（WRS）、Reconnection Shift
│   └── io/          # EXR / PNG / PFM
├── tests/           # 自研断言框架；WRS/merge/reconnection/visibility/BVH 测试
├── tools/           # Python：实验驱动、指标、绘图
└── results/         # 实验原始数据与图像
```

## 设计要点与简化边界

见 `docs/01-design.md`（数学推导：RIS/WRS/reconnection shift）与 `docs/00-selection.md`（选型理由）。核心简化：Lambertian 材质、面光源、一阶间接路径（x0→x1→光源）、reconnect-to-vertex（Jacobian=1）、纯空间复用（无时域）。
