#pragma once
// =============================================================================
// restir_renderer.h — ReSTIR GI 渲染器（Ouyang 2021 简化版）
//
// 三阶段流水线：
//   1. G-Buffer + Initial Sampling（每像素 spp 个初始候选）
//   2. Spatial Reuse（屏幕空间邻域 reservoir 合并，可开关）
//   3. Shading（直接光照 + W·f(y)）
// =============================================================================
#include "renderer/stats.h"
#include "scene/scene.h"
#include "core/camera.h"
#include "io/image.h"
#include "restir/shift.h"
#include "restir/reservoir.h"

namespace restir {

class ReSTIRRenderer {
public:
    Image render(const Scene& scene, const Camera& cam, const RenderConfig& cfg, Stats& stats);

private:
    void pass_gbuffer(const Scene& scene, const Camera& cam, const RenderConfig& cfg, Stats& stats);
    void pass_initial(const Scene& scene, const RenderConfig& cfg, Stats& stats);
    void pass_spatial_reuse(const Scene& scene, const RenderConfig& cfg, Stats& stats);
    void pass_shade(const Scene& scene, const RenderConfig& cfg, Image& img, Stats& stats);

    int W_ = 0, H_ = 0;
    std::vector<GBufferPixel> gbuf_;
    std::vector<Reservoir>    reservoirs_;      // initial 结果（pass 2 只读）
    std::vector<Reservoir>    reused_;          // spatial reuse 结果
    std::vector<vec3>         direct_;          // 每像素直接光照平均
};

} // namespace restir
