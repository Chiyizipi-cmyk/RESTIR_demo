// 可见性检测行为：隔墙两侧的重连
//   unbiased → p̂ = 0（被墙遮挡，候选丢弃）
//   biased   → p̂ > 0（漏光路径被接受，即 E4 要量化的现象）
#include "tests.h"
#include "restir/shift.h"
#include <cmath>

namespace restir::test {

namespace {
// 地板 + 天花 + x=0 处隔墙（y∈[0,3]，z∈[-5,5]，双面）
Scene make_wall_scene() {
    Scene s;
    s.materials.push_back(Material{vec3(0.8f), vec3(0)});
    // 地板 y=0
    s.triangles.push_back({{-5, 0, -5}, {5, 0, -5}, {5, 0, 5}, 0});
    s.triangles.push_back({{-5, 0, -5}, {5, 0, 5}, {-5, 0, 5}, 0});
    // 天花 y=2
    s.triangles.push_back({{-5, 2, -5}, {-5, 2, 5}, {5, 2, 5}, 0});
    s.triangles.push_back({{-5, 2, -5}, {5, 2, 5}, {5, 2, -5}, 0});
    // 隔墙 x=0（双面）
    s.triangles.push_back({{0, 0, -5}, {0, 3, -5}, {0, 3, 5}, 0});
    s.triangles.push_back({{0, 0, -5}, {0, 3, 5}, {0, 0, 5}, 0});
    s.triangles.push_back({{0, 0, -5}, {0, 0, 5}, {0, 3, 5}, 0});
    s.triangles.push_back({{0, 0, -5}, {0, 3, 5}, {0, 3, -5}, 0});
    s.finalize();
    return s;
}
} // namespace

void test_visibility(Context& ctx) {
    Scene scene = make_wall_scene();
    Stats stats;

    // 当前像素：墙右侧地板上 (1, 0, 0)，法线向上
    GBufferPixel g;
    g.x = vec3(1, 0, 0); g.n = vec3(0, 1, 0);
    g.albedo = vec3(0.8f); g.Le = vec3(0); g.valid = 1;

    // 邻居样本：x1 在墙左侧天花上 (-1, 2, 1) —— 重连线穿过隔墙
    PathSample s;
    s.x1 = vec3(-1, 2, 1); s.n1 = vec3(0, -1, 0);
    s.suffix = vec3(3.0f); s.pdf_light = 1.0f; s.valid = 1;

    ShiftResult sb = evaluate_reconnect(scene, g, s, /*unbiased=*/false, stats);
    ShiftResult su = evaluate_reconnect(scene, g, s, /*unbiased=*/true, stats);

    // biased：忽略遮挡 → 贡献 > 0（漏光路径）
    CHECK_TRUE(ctx, sb.p_hat > 0.0f);
    // unbiased：可见性检测 → 候选被拒绝
    CHECK_TRUE(ctx, !su.visible);
    CHECK_TRUE(ctx, su.p_hat == 0.0f);

    // 无遮挡对照：x1 在同侧天花上，重连线不穿墙
    PathSample s2 = s; s2.x1 = vec3(1.5f, 2, 0.5f);
    ShiftResult su2 = evaluate_reconnect(scene, g, s2, /*unbiased=*/true, stats);
    CHECK_TRUE(ctx, su2.visible);
    CHECK_TRUE(ctx, su2.p_hat > 0.0f);
}

} // namespace restir::test
