#pragma once
// =============================================================================
// shift.h — Reconnection Shift（Ouyang 2021 §3.2 / Lin 2022 GRIS 框架）
//
// 把邻居路径样本 z' = (x0', x1', l') 重连到当前像素 x0：
//     T(z') = (x0, x1', l')
// 因 x1' 为场景表面顶点（面积测度恒等嵌入），Jacobian = 1。
// biased 模式省略 V(x0, x1') 检测 → 漏光；unbiased 模式完整检测。
// =============================================================================
#include "scene/scene.h"
#include "renderer/stats.h"
#include "restir/reservoir.h"

namespace restir {

// G-Buffer 像素：重连的“前缀”
struct GBufferPixel {
    vec3 x, n, albedo, Le;
    int  valid = 0;
};

// 重连评估：返回当前像素上下文下的目标函数值与完整贡献
// unbiased=true 时发射 shadow ray 检测 V(x0, x1)
struct ShiftResult {
    float p_hat;
    vec3  f;      // 完整路径贡献（vec3）
    bool  visible;
};

ShiftResult evaluate_reconnect(const Scene& scene, const GBufferPixel& g, const PathSample& s,
                               bool unbiased, Stats& stats);

} // namespace restir
