#include "restir/shift.h"
#include "core/math_utils.h"
#include <cmath>

namespace restir {

ShiftResult evaluate_reconnect(const Scene& scene, const GBufferPixel& g, const PathSample& s,
                               bool unbiased, Stats& stats) {
    ShiftResult out{0.0f, vec3(0.0f), false};
    if (!g.valid || !s.valid) return out;

    vec3 dir = s.x1 - g.x;
    float dist2 = glm::dot(dir, dir);
    if (dist2 < 4.0f * RAY_EPS * RAY_EPS) return out;
    float dist = std::sqrt(dist2);
    vec3  wi   = dir / dist;

    float cos0 = std::max(0.0f, glm::dot(g.n, wi));
    float cos1 = std::max(0.0f, glm::dot(s.n1, -wi));
    if (cos0 <= 0.0f || cos1 <= 0.0f) return out;

    // 几何项（不含可见性）：G(x0,x1) = cos0·cos1 / dist²
    vec3 f = (g.albedo * INV_PI) * cos0 * cos1 / dist2 * s.suffix;

    bool vis = true;
    if (unbiased) {
        stats.shadow_reuse++;
        vis = scene.visible(g.x + g.n * RAY_EPS, s.x1 + s.n1 * RAY_EPS);
        if (!vis) return out; // 不可见：p̂ = 0，候选权重 0
    }

    out.p_hat   = luminance(f);
    out.f       = f;
    out.visible = vis;
    return out;
}

} // namespace restir
