// 直接光照（NEE）正确性 —— 覆盖历史缺陷：面光源几何法线背离房间时
// light_contribution 的 cos_l 恒为 0，NEE 静默失效，退化为纯 BSDF 采样。
//
//  1. 朝向约定：场景中所有面光源的法线必须指向被照亮的一侧
//  2. 双估计量交叉验证：NEE 估计与 BSDF 命中估计必须收敛到同一期望
//  3. 渲染级回归：仅开启 NEE（max-depth = 1）时房间必须被照亮
#include "tests.h"
#include "renderer/lighting.h"
#include "renderer/pathtracer.h"
#include <cmath>

namespace restir::test {

namespace {

// 地板（y=0，覆盖 [-2,2]²）+ 1×1 面光源（y=1，法线朝下）
Scene make_flat_scene() {
    Scene s;
    s.materials.push_back(Material{vec3(0.8f), vec3(0.0f)});
    s.materials.push_back(Material{vec3(0.0f), vec3(5.0f)});
    s.triangles.push_back({{-2, 0, -2}, {2, 0, -2}, {2, 0, 2}, 0});
    s.triangles.push_back({{-2, 0, -2}, {2, 0, 2}, {-2, 0, 2}, 0});
    // 法线朝下（-y）：绕序与 scenes.cpp 的 add_ceiling_light_quad 一致
    s.triangles.push_back({{0.5f, 1, -0.5f}, {0.5f, 1, 0.5f}, {-0.5f, 1, 0.5f}, 1});
    s.triangles.push_back({{0.5f, 1, -0.5f}, {-0.5f, 1, 0.5f}, {-0.5f, 1, -0.5f}, 1});
    s.finalize();
    return s;
}

} // namespace

void test_lighting(Context& ctx) {
    // ---- 1. 朝向约定：cornell/occlusion/hdr 的面光源法线必须指向房间内部 ----
    for (const char* name : {"cornell", "occlusion", "hdr"}) {
        Scene s = build_scene(name);
        CHECK_TRUE(ctx, !s.lights.empty());
        for (const AreaLight& al : s.lights) {
            vec3 n = triangle_normal(s.triangles[al.tri_id]);
            CHECK_TRUE(ctx, n.y < -0.9f); // 水平天花光源，法线应朝下
        }
    }

    // ---- 2. NEE 与 BSDF 命中两种估计量交叉验证 ----
    Scene scene = make_flat_scene();
    Stats stats;
    const vec3  x(0.0f, 0.0f, 0.0f), n(0, 1, 0), albedo(0.8f);
    const int   N = 200000;
    pcg32 rng_nee(2024, 17), rng_bsdf(2024, 29);
    double sum_nee = 0.0, sum_bsdf = 0.0;
    int    hits = 0;

    for (int i = 0; i < N; ++i) {
        // (a) NEE：采样光源点，按 f·Le·cos_x·cos_l / (d²·pdf) 求贡献
        LightSample ls = sample_light(scene, rng_nee);
        sum_nee += light_contribution(scene, x, n, albedo, ls, stats).x;

        // (b) BSDF 命中：余弦采样方向，命中发光面正面时按 albedo·Le 计入
        float pdf_w;
        vec3  wi = cosine_sample_hemisphere(n, rng_bsdf, pdf_w);
        Hit   h;
        if (scene.intersect(Ray{x + n * RAY_EPS, wi}, h)) {
            const Material& m = scene.materials[h.material_id];
            if (h.front && luminance(m.emission) > 0.0f) {
                float cos_t = glm::dot(n, wi);
                sum_bsdf += ((albedo * INV_PI) * cos_t / pdf_w * m.emission).x;
                ++hits;
            }
        }
    }
    double est_nee = sum_nee / N, est_bsdf = sum_bsdf / N;
    CHECK_TRUE(ctx, est_nee > 0.0);              // 关键回归：NEE 必须真的在发光
    CHECK_TRUE(ctx, hits > N / 10);              // BSDF 采样确实命中过光源
    CHECK_NEAR(ctx, est_nee, est_bsdf, 0.05);    // 两条独立路径收敛到同一期望

    // ---- 2b. 反例：法线翻转的光源必须得到 0 贡献（记录失效模式本身） ----
    Scene flipped = make_flat_scene();
    for (Triangle& t : flipped.triangles)
        if (luminance(flipped.materials[t.material_id].emission) > 0.0f)
            std::swap(t.v1, t.v2); // 反转绕序 → 法线朝上
    LightSample lsf = sample_light(flipped, rng_nee);
    vec3 bad = light_contribution(flipped, x, n, albedo, lsf, stats);
    CHECK_TRUE(ctx, bad.x == 0.0f);

    // ---- 3. 渲染级回归：max-depth=1（仅 NEE）时房间必须被照亮 ----
    Scene  cornell = build_scene("cornell");
    Camera cam     = build_camera("cornell", 32, 32);
    RenderConfig cfg;
    cfg.width = 32; cfg.height = 32; cfg.spp = 8; cfg.max_depth = 1; cfg.seed = 7;
    cfg.write_exr = cfg.write_png = cfg.write_pfm = false;
    Stats st;
    PathTracer pt;
    Image img = pt.render(cornell, cam, cfg, st);

    int lit = 0;
    for (const vec3& p : img.pixels) if (luminance(p) > 1e-3f) ++lit;
    double lit_frac = (double)lit / (double)img.pixels.size();
    // 修复前该值约 2%（只有光源像素与偶然命中），修复后绝大多数像素被 NEE 照亮
    CHECK_TRUE(ctx, lit_frac > 0.5);
}

} // namespace restir::test
