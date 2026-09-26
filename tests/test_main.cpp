// 单元测试入口：运行全部测试，任何失败返回非零
#include "tests.h"
#include <cstdio>

int main() {
    using namespace restir::test;
    Context ctx;
    struct { const char* name; void (*fn)(Context&); } suites[] = {
        {"rng",              test_rng},
        {"wrs",              test_wrs},
        {"reservoir_merge",  test_reservoir_merge},
        {"reconnection",     test_reconnection},
        {"visibility",       test_visibility},
        {"bvh",              test_bvh},
        {"render_sanity",    test_render_sanity},
    };
    for (auto& s : suites) {
        int before = ctx.failures;
        s.fn(ctx);
        std::printf("[%s] %s\n", s.name, (ctx.failures == before) ? "PASS" : "FAIL");
    }
    std::printf("checks=%d failures=%d\n", ctx.checks, ctx.failures);
    return ctx.failures == 0 ? 0 : 1;
}
