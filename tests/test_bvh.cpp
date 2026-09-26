// BVH 正确性：与暴力遍历逐三角形求交结果一致
#include "tests.h"
#include "scene/scene.h"
#include <cmath>

namespace restir::test {

namespace {
// 暴力求交（不经过 BVH）
bool brute_force(const Scene& s, const Ray& ray, float& t_best) {
    bool found = false;
    t_best = ray.tmax;
    for (size_t i = 0; i < s.triangles.size(); ++i) {
        const Triangle& tri = s.triangles[i];
        vec3 e1 = tri.v1 - tri.v0, e2 = tri.v2 - tri.v0;
        vec3 p = glm::cross(ray.d, e2);
        float det = glm::dot(e1, p);
        if (std::fabs(det) < 1e-12f) continue;
        float inv = 1.0f / det;
        vec3 tv = ray.o - tri.v0;
        float u = glm::dot(tv, p) * inv;
        if (u < 0.f || u > 1.f) continue;
        vec3 qv = glm::cross(tv, e1);
        float v = glm::dot(ray.d, qv) * inv;
        if (v < 0.f || u + v > 1.f) continue;
        float t = glm::dot(e2, qv) * inv;
        if (t < ray.tmin || t > t_best) continue;
        t_best = t; found = true;
    }
    return found;
}
} // namespace

void test_bvh(Context& ctx) {
    Scene scene = build_scene("cornell");
    pcg32 rng(555, 1);
    const int N = 20000;
    int mismatch = 0;
    for (int i = 0; i < N; ++i) {
        // 在包围盒附近随机生成射线
        vec3 o(rng.next_f32(-0.5f, 1.5f), rng.next_f32(-0.5f, 1.5f), rng.next_f32(-2.0f, -0.1f));
        vec3 d(rng.next_f32(-1, 1), rng.next_f32(-1, 1), rng.next_f32(0.2f, 1));
        d = glm::normalize(d);
        Ray ray{o, d};
        Hit hit;
        bool bvh_hit = scene.intersect(ray, hit);
        float t_brute;
        bool brute_hit = brute_force(scene, ray, t_brute);
        if (bvh_hit != brute_hit) { ++mismatch; continue; }
        if (bvh_hit && std::fabs(hit.t - t_brute) > 1e-4f) ++mismatch;
    }
    CHECK_TRUE(ctx, mismatch == 0);
}

} // namespace restir::test
