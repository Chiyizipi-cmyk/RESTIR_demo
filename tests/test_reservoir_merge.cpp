// Reservoir 合并：W 值与论文公式一致（Lin 2022 §4 / Bitterli 2020 Alg.4）
#include "tests.h"
#include "restir/reservoir.h"
#include <cmath>

namespace restir::test {

static PathSample tagged(float tag) {
    PathSample s;
    s.x1 = vec3(tag); s.n1 = vec3(0, 1, 0); s.suffix = vec3(tag);
    s.pdf_light = 1.f; s.valid = 1;
    return s;
}

void test_reservoir_merge(Context& ctx) {
    pcg32 rng(99, 1);

    // 手算验证：r1{y=1, w_sum=10, M=2, p̂=5} → W1 = 10/(2·5)=1
    Reservoir r1; r1.y = tagged(1); r1.w_sum = 10; r1.M = 2; r1.p_hat_y = 5;
    reservoir_finalize(r1);
    CHECK_NEAR(ctx, r1.W, 1.0, 1e-6);

    // r2{y=2, w_sum=6, M=3, p̂=2} → W2 = 6/(3·2)=1
    Reservoir r2; r2.y = tagged(2); r2.w_sum = 6; r2.M = 3; r2.p_hat_y = 2;
    reservoir_finalize(r2);
    CHECK_NEAR(ctx, r2.W, 1.0, 1e-6);

    // 合并（当前上下文 p̂_q 分别为 4 和 8）：候选权重 = p̂_q·W·M = 4·1·2=8, 8·1·3=24
    // 确定性检查 M 与 w_sum
    {
        Reservoir c;
        reservoir_merge(c, r1, 4.0f, 100, rng);
        reservoir_merge(c, r2, 8.0f, 100, rng);
        CHECK_TRUE(ctx, c.M == 5);
        CHECK_NEAR(ctx, c.w_sum, 32.0, 1e-5);
        CHECK_TRUE(ctx, c.y.suffix.x == 1.0f || c.y.suffix.x == 2.0f);
        // 选 y=2 (p̂=8) 时 W = 32/(5·8) = 0.8；选 y=1 (p̂=4) 时 W = 32/(5·4)=1.6
        float expect_phat = (c.y.suffix.x == 1.0f) ? 4.0f : 8.0f;
        reservoir_finalize(c);
        CHECK_NEAR(ctx, c.W, 32.0 / (5.0 * expect_phat), 1e-5);
    }

    // 统计验证：选择概率 ∝ p̂_q·W·M（8 vs 24 → 25% / 75%）
    {
        const int N = 100000;
        long long pick1 = 0;
        for (int t = 0; t < N; ++t) {
            Reservoir c;
            reservoir_merge(c, r1, 4.0f, 100, rng);
            reservoir_merge(c, r2, 8.0f, 100, rng);
            if (c.y.suffix.x == 1.0f) pick1++;
        }
        CHECK_NEAR(ctx, (double)pick1 / N, 0.25, 0.01);
    }

    // M 截断：r.M=1000, m_cap=30 → 仅计 30
    {
        Reservoir big; big.y = tagged(3); big.w_sum = 3000; big.M = 1000; big.p_hat_y = 1;
        reservoir_finalize(big); // W = 3000/(1000·1) = 3
        Reservoir c;
        reservoir_merge(c, big, 1.0f, 30, rng);
        CHECK_TRUE(ctx, c.M == 30);
        CHECK_NEAR(ctx, c.w_sum, 1.0 * 3.0 * 30.0, 1e-4);
    }
}

} // namespace restir::test
