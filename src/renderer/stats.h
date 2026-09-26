#pragma once
// =============================================================================
// stats.h — 射线计数与渲染统计（按类别拆分，AGENT.md §7.1 要求）
//
// 原子计数仅用于统计，不影响数值结果 → 不破坏确定性。
// =============================================================================
#include <atomic>
#include <cstdint>
#include <string>

namespace restir {

struct Stats {
    std::atomic<uint64_t> primary_rays{0};   // 相机首命中
    std::atomic<uint64_t> gi_rays{0};        // 间接路径反弹（PT 全部 bounce / ReSTIR initial）
    std::atomic<uint64_t> shadow_nee{0};     // NEE 光源可见性
    std::atomic<uint64_t> shadow_reuse{0};   // 空间复用重连可见性（unbiased 模式）

    uint64_t total_rays() const {
        return primary_rays + gi_rays + shadow_nee + shadow_reuse;
    }
    void reset() {
        primary_rays = 0; gi_rays = 0; shadow_nee = 0; shadow_reuse = 0;
    }
};

struct RenderConfig {
    // 场景与方法
    std::string scene  = "cornell";
    std::string method = "restir";   // pt | restir
    int tri_budget = 0;              // E6：>0 时 cornell 场景填充网格盒子
    // 图像
    int width = 128, height = 128;
    // 采样
    int      spp       = 1;
    int      max_depth = 4;          // PT 最大表面顶点数（含首命中）
    uint64_t seed      = 42;
    // ReSTIR 开关
    bool reuse_spatial = true;
    bool biased        = true;
    int  spatial_radius = 2;
    int  candidates     = 5;
    int  m_cap          = 30;        // reservoir M 上限（防退化）
    // 并行与输出
    int         threads    = 0;      // 0 = hardware_concurrency
    std::string output     = "results/out";
    bool        write_exr  = true;
    bool        write_png  = true;
    bool        write_pfm  = true;
};

} // namespace restir
