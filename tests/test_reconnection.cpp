// Reconnection Shift 正确性（无遮挡场景）：
//  1. 重连评估与直接路径公式一致（几何/BSDF 逐项手算对照）
//  2. 统计一致性：initial reservoir 的 W·f(y) 期望 == 暴力 MC 的 f/q 期望
#include "tests.h"
#include "restir/shift.h"
#include "renderer/lighting.h"
#include <cmath>

namespace restir::test {

namespace {
// 极简无遮挡场景：地板 + 后墙 + 天花板面光源
Scene make_flat_scene() {
    Scene s;
    s.materials.push_back(Material{vec3(0.8f), vec3(0)});    // 0 漫反射
    s.materials.push_back(Material{vec3(0.0f), vec3(4.0f)}); // 1 光源
    // 地板 y=0
    s.triangles.push_back({{-2, 0, -2}, {2, 0, -2}, {2, 0, 2}, 0});
    s.triangles.push_back({{-2, 0, -2}, {2, 0, 2}, {-2, 0, 2}, 0});
    // 后墙 z=2
    s.triangles.push_back({{-2, 0, 2}, {2, 0, 2}, {2, 3, 2}, 0});
    s.triangles.push_back({{-2, 0, 2}, {2, 3, 2}, {-2, 3, 2}, 0});
    // 光源 y=3，1×1 居中
    s.triangles.push_back({{-0.5f, 3, -0.5f}, {-0.5f, 3, 0.5f}, {0.5f, 3, 0.5f}, 1});
    s.triangles.push_back({{-0.5f, 3, -0.5f}, {0.5f, 3, 0.5f}, {0.5f, 3, -0.5f}, 1});
    s.finalize();
    return s;
}
} // namespace

void test_reconnection(Context& ctx) {
    Scene scene = make_flat_scene();
    Stats stats;

    // 当前像素 G-Buffer：地板上的点，法线向上
    GBufferPixel g;
    g.x = vec3(0.1f, 0, 0.1f); g.n = vec3(0, 1, 0);
    g.albedo = vec3(0.8f); g.Le = vec3(0); g.valid = 1;

    // 手工路径样本：x1 在后墙上（有抬升方向，cos0 > 0），suffix 已知
    PathSample s;
    s.x1 = vec3(0.5f, 1.0f, 1.9f); s.n1 = vec3(0, 0, -1);
    s.suffix = vec3(1.5f); s.pdf_light = 1.0f; s.valid = 1;

    // 1) 重连评估 == 手算公式
    ShiftResult sr = evaluate_reconnect(scene, g, s, /*unbiased=*/false, stats);
    vec3  dir = s.x1 - g.x;
    float dist2 = glm::dot(dir, dir);
    float dist = std::sqrt(dist2);
    float cos0 = dir.y / dist;            // dot(n0, wi)
    float cos1 = dir.z / dist;            // dot(n1, -wi), n1 = (0,0,-1)
    vec3 expect = (g.albedo * INV_PI) * cos0 * cos1 / dist2 * s.suffix;
    CHECK_NEAR(ctx, sr.f.x, expect.x, 1e-6);
    CHECK_NEAR(ctx, sr.p_hat, luminance(expect), 1e-6);
    CHECK_TRUE(ctx, sr.p_hat > 0.0f);

    // unbiased：无遮挡 → V=1，与 biased 一致
    ShiftResult su = evaluate_reconnect(scene, g, s, /*unbiased=*/true, stats);
    CHECK_TRUE(ctx, su.visible);
    CHECK_NEAR(ctx, su.p_hat, sr.p_hat, 1e-6);

    // 2) 统计一致性：M=1 时 W·f(y) 逐样本 == f/q（reservoir 不改变估计量）
    const int N = 4000;
    pcg32 rng(1234, 5);
    double restir_est = 0.0, mc_est = 0.0;
    int valid_count = 0;
    for (int i = 0; i < N; ++i) {
        float pdf_w;
        vec3 wi = cosine_sample_hemisphere(g.n, rng, pdf_w);
        Ray ray{g.x + g.n * RAY_EPS, wi};
        Hit hit;
        if (scene.intersect(ray, hit)) {
            const Material& m1 = scene.materials[hit.material_id];
            // 显式采样光源（与 renderer 流程一致）
            LightSample ls = sample_light(scene, rng);
            vec3 suffix = m1.emission + light_contribution(scene, hit.p, hit.n, m1.albedo, ls, stats);
            float cos0 = std::max(0.0f, glm::dot(g.n, wi));
            float cos1 = std::max(0.0f, glm::dot(hit.n, -wi));
            float d2 = glm::dot(hit.p - g.x, hit.p - g.x);
            vec3  f = (g.albedo * INV_PI) * cos0 * cos1 / d2 * suffix;
            float p_hat = luminance(f);
            // q(z) = p_A(x1) = p_ω(ω1)·cos1/d²：光源 pdf 已包含在 suffix 的 NEE 估计内，
            // 不得重复计入（与 restir_renderer.cpp pass_initial 口径一致）
            float q = pdf_w * cos1 / d2;
            float w = (q > 0.f && p_hat > 0.f) ? p_hat / q : 0.f;

            PathSample z;
            z.x1 = hit.p; z.n1 = hit.n; z.suffix = suffix; z.pdf_light = ls.pdf; z.valid = 1;
            Reservoir R;
            reservoir_update(R, z, w, p_hat, rng);
            reservoir_finalize(R);
            restir_est += (f * R.W).x;
            if (q > 0.f) { mc_est += (double)f.x / q; ++valid_count; }
        }
    }
    CHECK_TRUE(ctx, valid_count > N / 10); // 足够多有效样本
    // M=1 时 reservoir 输出权重 W = 1/q，两个估计量应逐样本一致
    CHECK_NEAR(ctx, restir_est, mc_est, 1e-4);
}

} // namespace restir::test
