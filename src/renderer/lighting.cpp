#include "renderer/lighting.h"

namespace restir {

LightSample sample_light(const Scene& scene, pcg32& rng) {
    // 优先面光源；无面光源则回退到点光源（点光源可选，默认关闭）
    if (!scene.lights.empty()) {
        float u = rng.next_f32() * scene.total_light_area;
        // 线性搜索 CDF（场景面光源数通常很少 < 100）
        int idx = 0;
        for (; idx < (int)scene.light_cdf.size(); ++idx) {
            if (scene.light_cdf[idx] >= u) break;
        }
        idx = std::min(idx, (int)scene.light_cdf.size() - 1);
        const AreaLight& al = scene.lights[idx];
        const Triangle&  t  = scene.triangles[al.tri_id];
        vec3 lp = sample_triangle(t.v0, t.v1, t.v2, rng);
        vec3 ln = glm::normalize(glm::cross(t.v1 - t.v0, t.v2 - t.v0));
        vec3 em = scene.materials[t.material_id].emission;
        // 面积测度 pdf = 1/total_area（选三角形 ∝ 面积 × 三角形内均匀）
        return LightSample{lp, ln, em, 1.0f / scene.total_light_area, al.tri_id, false};
    }
    if (scene.point_light.enabled) {
        return LightSample{scene.point_light.pos, vec3(0, 0, 0), scene.point_light.intensity,
                           1.0f, -1, true};
    }
    return LightSample{};
}

vec3 light_contribution(const Scene& scene, const vec3& x, const vec3& n, const vec3& albedo,
                        const LightSample& ls, Stats& stats) {
    vec3 dir = ls.p - x;
    float dist2 = glm::dot(dir, dir);
    float dist  = std::sqrt(dist2);
    if (dist < 2.0f * RAY_EPS) return vec3(0.0f);
    vec3 wi = dir / dist;
    float cos_x = std::max(0.0f, glm::dot(n, wi));
    if (cos_x <= 0.0f) return vec3(0.0f);

    // 可见性
    stats.shadow_nee++;
    if (!scene.visible(x + n * RAY_EPS, ls.p)) return vec3(0.0f);

    vec3 brdf = albedo * INV_PI; // Lambertian
    if (ls.is_point) {
        // 点光源：强度 I（W/sr）→ radiance 贡献 = I / dist²
        return brdf * ls.Le * cos_x / dist2;
    }
    float cos_l = std::max(0.0f, -glm::dot(ls.n, wi));
    return brdf * ls.Le * cos_x * cos_l / (dist2 * ls.pdf);
}

vec3 direct_lighting(const Scene& scene, const vec3& x, const vec3& n, const vec3& albedo,
                     pcg32& rng, Stats& stats) {
    LightSample ls = sample_light(scene, rng);
    if (ls.pdf <= 0.0f && !ls.is_point) return vec3(0.0f);
    return light_contribution(scene, x, n, albedo, ls, stats);
}

} // namespace restir
