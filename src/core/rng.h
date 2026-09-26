#pragma once
// =============================================================================
// rng.h — PCG32 随机数发生器（每像素独立流，保证确定性并行）
//
// 可复现性方案（docs/00-selection.md §4）：
//   每个像素使用 pcg32(global_seed, pixel_index) 独立播种；
//   空间复用的邻居选择使用 pcg32(global_seed ^ K, pixel_index) 独立流；
//   结果与遍历顺序、线程数无关 → 同命令同种子逐位一致。
// =============================================================================
#include <cstdint>

namespace restir {

struct pcg32 {
    uint64_t state = 0;
    uint64_t inc   = 0;

    pcg32() = default;
    pcg32(uint64_t seed, uint64_t seq) { seed_rng(seed, seq); }

    void seed_rng(uint64_t seed, uint64_t seq) {
        state = 0;
        inc   = (seq << 1u) | 1u;
        next_u32();
        state += seed;
        next_u32();
    }

    uint32_t next_u32() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot        = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // 均匀浮点 [0,1)
    float next_f32() { return (float)(next_u32() >> 8) * (1.0f / 16777216.0f); }
    // 均匀浮点 [a,b)
    float next_f32(float a, float b) { return a + next_f32() * (b - a); }

    // 均匀整数 [0,bound)，无模偏差（Lemire 拒绝采样）
    uint32_t next_u32_bounded(uint32_t bound) {
        uint32_t threshold = (0u - bound) % bound;
        for (;;) {
            uint32_t r = next_u32();
            if (r >= threshold) return r % bound;
        }
    }
};

} // namespace restir
