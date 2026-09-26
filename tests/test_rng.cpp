// PCG32：确定性 + 均匀性（均值/方差/卡方分桶）
#include "tests.h"
#include "core/rng.h"
#include <cmath>

namespace restir::test {

void test_rng(Context& ctx) {
    // 同种子同序列 → 逐位一致
    pcg32 a(42, 7), b(42, 7);
    bool same = true;
    for (int i = 0; i < 1000; ++i) same &= (a.next_u32() == b.next_u32());
    CHECK_TRUE(ctx, same);

    // 不同像素序列 → 流独立
    pcg32 c(42, 8);
    bool diff = false;
    pcg32 d(42, 7);
    for (int i = 0; i < 10; ++i) diff |= (c.next_u32() != d.next_u32());
    CHECK_TRUE(ctx, diff);

    // 均匀性：1M 样本，均值≈0.5，方差≈1/12，10 桶卡方
    pcg32 r(123, 456);
    const int N = 1000000, B = 10;
    double mean = 0, m2 = 0;
    long long buckets[B] = {0};
    for (int i = 0; i < N; ++i) {
        double x = r.next_f32();
        mean += (x - mean) / (i + 1);
        m2   += (x - mean) * (x - 0.5);
        int b = (int)(x * B);
        if (b >= B) b = B - 1;
        buckets[b]++;
    }
    double var = m2 / N;
    CHECK_NEAR(ctx, mean, 0.5, 0.005);
    CHECK_NEAR(ctx, var, 1.0 / 12.0, 0.02);
    double chi2 = 0;
    for (int i = 0; i < B; ++i) {
        double expect = (double)N / B;
        chi2 += (buckets[i] - expect) * (buckets[i] - expect) / expect;
    }
    // df=9，0.1% 临界值 ≈ 27.9
    CHECK_TRUE(ctx, chi2 < 27.9);
}

} // namespace restir::test
