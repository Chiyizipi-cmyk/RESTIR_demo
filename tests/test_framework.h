#pragma once
// =============================================================================
// test_framework.h — 自研最小断言框架（AGENT.md 允许 Catch2 或自研）
// =============================================================================
#include <cstdio>
#include <cmath>

namespace restir::test {

struct Context {
    int checks = 0;
    int failures = 0;
};

#define CHECK_TRUE(ctx, cond)                                                        \
    do {                                                                             \
        ++(ctx).checks;                                                              \
        if (!(cond)) {                                                               \
            ++(ctx).failures;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(ctx, a, b, tol)                                                   \
    do {                                                                             \
        ++(ctx).checks;                                                              \
        double _va = (double)(a), _vb = (double)(b);                                 \
        double _scale = std::max(1.0, std::fabs(_vb));                               \
        if (std::fabs(_va - _vb) > (tol) * _scale) {                                 \
            ++(ctx).failures;                                                        \
            std::printf("  FAIL %s:%d: %s=%.9g 期望 %s=%.9g (tol=%.3g)\n",           \
                        __FILE__, __LINE__, #a, _va, #b, _vb, (double)(tol));        \
        }                                                                            \
    } while (0)

} // namespace restir::test
