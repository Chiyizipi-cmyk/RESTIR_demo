// WRS 大样本统计检验：选择频率 ∝ 权重；W 值与公式一致
#include "tests.h"
#include "restir/reservoir.h"
#include <cmath>

namespace restir::test {

static PathSample make_sample(float tag) {
    PathSample s;
    s.x1 = vec3(tag, 0, 0);
    s.n1 = vec3(0, 1, 0);
    s.suffix = vec3(tag);
    s.pdf_light = 1.0f;
    s.valid = 1;
    return s;
}

void test_wrs(Context& ctx) {
    // 4 个候选，权重 {1,2,3,4}，200k 次独立试验统计选择频率
    const float w[4] = {1.f, 2.f, 3.f, 4.f};
    const int   N = 200000;
    long long   pick[4] = {0, 0, 0, 0};
    pcg32 rng(7, 7);
    for (int t = 0; t < N; ++t) {
        Reservoir r;
        for (int i = 0; i < 4; ++i)
            reservoir_update(r, make_sample((float)(i + 1)), w[i], w[i], rng);
        int sel = (int)r.y.suffix.x - 1;
        if (sel >= 0 && sel < 4) pick[sel]++;
        // W 公式：W = w_sum / (M · p̂(y))
        reservoir_finalize(r);
        CHECK_NEAR(ctx, r.W, r.w_sum / (4.0 * (double)r.p_hat_y), 1e-5);
    }
    double wsum = 10.0;
    for (int i = 0; i < 4; ++i)
        CHECK_NEAR(ctx, (double)pick[i] / N, w[i] / wsum, 0.01);

    // 单候选：M=1，W = 1/q（当 w = p̂/q）
    Reservoir r1;
    reservoir_update(r1, make_sample(1.0f), 2.5f /*=p̂/q*/, 5.0f /*p̂*/, rng);
    reservoir_finalize(r1);
    CHECK_NEAR(ctx, r1.W, 0.5, 1e-6); // 2.5/(1*5) = 0.5 = 1/q

    // 全零权重：不崩溃，W=0
    Reservoir r0;
    for (int i = 0; i < 4; ++i) reservoir_update(r0, make_sample(1.0f), 0.0f, 0.0f, rng);
    reservoir_finalize(r0);
    CHECK_TRUE(ctx, r0.W == 0.0f);
    CHECK_TRUE(ctx, r0.M == 4);
}

} // namespace restir::test
