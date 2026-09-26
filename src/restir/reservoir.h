#pragma once
// =============================================================================
// reservoir.h — ReSTIR 核心数据结构
//
// PathSample: 一条可复用的间接路径样本（x0 为像素固定首命中，x1 为间接顶点）
// Reservoir : WRS 状态（y, w_sum, M, W），对应 Bitterli 2020 §3
// =============================================================================
#include <glm/glm.hpp>
#include "core/math_utils.h"

namespace restir {

struct PathSample {
    vec3  x1;        // 间接顶点位置
    vec3  n1;        // x1 处法线
    vec3  suffix;    // 不变量部分：Le(x1) + NEE(x1)（与重连无关）
    float pdf_light; // p_A(l)（面积测度），仅统计/调试
    int   valid;     // 0 = dead（miss / 背面 / 光源不可达）
};

struct Reservoir {
    PathSample y;
    float w_sum   = 0.0f;
    float p_hat_y = 0.0f; // 当前 y 在当前像素上下文的目标函数值
    int   M       = 0;
    float W       = 0.0f; // 输出权重
};

// 单样本 WRS 更新：以概率 w/w_sum 替换 y（Bitterli 2020 Alg.1）
inline void reservoir_update(Reservoir& r, const PathSample& s, float w, float p_hat_s, pcg32& rng) {
    r.w_sum += w;
    ++r.M;
    if (w > 0.0f && rng.next_f32() < w / r.w_sum) {
        r.y       = s;
        r.p_hat_y = p_hat_s;
    }
}

// 合并：把邻居 reservoir r 的有效候选权重并入 c（Lin 2022 §4，M 截断防退化）
inline void reservoir_merge(Reservoir& c, const Reservoir& r, float p_hat_q, int m_cap, pcg32& rng) {
    if (r.M <= 0) return;
    int   m = (r.M < m_cap) ? r.M : m_cap;
    float w = p_hat_q * r.W * (float)m;
    c.w_sum += w;
    c.M     += m;
    if (w > 0.0f && rng.next_f32() < w / c.w_sum) {
        c.y       = r.y;
        c.p_hat_y = p_hat_q;
    }
}

// 输出权重：W = w_sum / (M · p̂(y))；p̂(y)=0 时置 0 防爆炸
inline float reservoir_finalize(Reservoir& r) {
    if (r.M <= 0 || r.p_hat_y <= 0.0f) { r.W = 0.0f; }
    else                               { r.W = r.w_sum / ((float)r.M * r.p_hat_y); }
    return r.W;
}

} // namespace restir
