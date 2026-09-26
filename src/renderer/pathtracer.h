#pragma once
// =============================================================================
// pathtracer.h — NEE Path Tracing 基线（ docs/01-design.md §6.2 ）
//
// 每个顶点执行 NEE（直接光照采样），间接方向按余弦加权 BSDF 采样，
// 无俄罗斯轮盘（固定深度 --max-depth），保证逐位可复现。
// =============================================================================
#include "renderer/stats.h"
#include "scene/scene.h"
#include "core/camera.h"
#include "core/rng.h"
#include "io/image.h"

namespace restir {

class PathTracer {
public:
    Image render(const Scene& scene, const Camera& cam, const RenderConfig& cfg, Stats& stats) const;
};

} // namespace restir
