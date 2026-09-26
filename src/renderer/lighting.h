#pragma once
// =============================================================================
// lighting.h — 光源采样与直接光照（PT 与 ReSTIR 共享，保证对比公平）
//
// 面光源：面积加权选择发光三角形 + 三角形内均匀采样，pdf = 1/total_area（面积测度）
// 点光源：退化 Dirac 分布，pdf 概念不适用（is_point 分支单独处理）
// =============================================================================
#include "scene/scene.h"
#include "renderer/stats.h"

namespace restir {

struct LightSample {
    vec3  p;         // 光源采样点
    vec3  n;         // 光源法线（面光源）
    vec3  Le;        // 辐射出射度（点光源时为强度 I）
    float pdf = 1.f; // 面积测度 pdf（点光源为 1）
    int   tri_id = -1;
    bool  is_point = false;
};

// 采样一个光源点（面光源：面积测度；点光源：固定点）
LightSample sample_light(const Scene& scene, pcg32& rng);

// 表面点 (x, n, albedo) 处由给定光源采样产生的直接光照贡献（含可见性检测）
// area:  L = f_r·Le·cos_x·cos_l·V / (dist²·pdf)
// point: L = f_r·I ·cos_x·V / dist²
vec3 light_contribution(const Scene& scene, const vec3& x, const vec3& n, const vec3& albedo,
                        const LightSample& ls, Stats& stats);

// 标准 NEE：采样 + 求贡献
vec3 direct_lighting(const Scene& scene, const vec3& x, const vec3& n, const vec3& albedo,
                     pcg32& rng, Stats& stats);

} // namespace restir
