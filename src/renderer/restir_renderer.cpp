// =============================================================================
// restir_renderer.cpp — ReSTIR GI 三阶段流水线实现
//
// 公式对应：
//   初始候选权重 w = p̂(z)/q(z)                (Talbot 2005 / Bitterli 2020 §2)
//   空间合并权重 w = p̂_q(T(z'))·W·M           (Lin 2022 §4, GRIS)
//   输出估计 L = W·f(y)                        (Bitterli 2020 Alg.4)
// =============================================================================
#include "renderer/restir_renderer.h"
#include "renderer/lighting.h"
#include <thread>
#include <algorithm>
#include <cmath>

namespace restir {

namespace {

// 静态行块并行（确定性：每像素独立 RNG 流，结果与线程数无关）
template <typename F>
void parallel_rows(int height, int threads, F&& fn) {
    int nt = threads > 0 ? threads : (int)std::thread::hardware_concurrency();
    if (nt < 1) nt = 1;
    int chunk = (height + nt - 1) / nt;
    std::vector<std::thread> pool;
    pool.reserve(nt);
    for (int t = 0; t < nt; ++t) {
        int y0 = t * chunk, y1 = std::min(y0 + chunk, height);
        if (y0 >= y1) continue;
        pool.emplace_back([&, y0, y1] { fn(y0, y1); });
    }
    for (auto& th : pool) th.join();
}

} // anonymous namespace

// ----------------------------- Pass 1: G-Buffer ------------------------------
void ReSTIRRenderer::pass_gbuffer(const Scene& scene, const Camera& cam, const RenderConfig& cfg,
                                  Stats& stats) {
    parallel_rows(H_, cfg.threads, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < W_; ++x) {
                int idx = y * W_ + x;
                GBufferPixel& g = gbuf_[idx];
                g = GBufferPixel{};
                stats.primary_rays++;
                Ray ray = cam.generate_ray_center(x, y);
                Hit hit;
                if (!scene.intersect(ray, hit)) { g.valid = 0; continue; }
                const Material& m = scene.materials[hit.material_id];
                g.x = hit.p; g.n = hit.n; g.albedo = m.albedo; g.Le = m.emission;
                // 单面发光：背面命中的发光面不作为光源/自发光像素处理（与 lighting.cpp 口径一致）
                if (!hit.front) g.Le = vec3(0.0f);
                g.valid = 1;
            }
        }
    });
}

// ------------------------ Pass 2: Initial Sampling ---------------------------
void ReSTIRRenderer::pass_initial(const Scene& scene, const RenderConfig& cfg, Stats& stats) {
    parallel_rows(H_, cfg.threads, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < W_; ++x) {
                int idx = y * W_ + x;
                Reservoir& R = reservoirs_[idx];
                R = Reservoir{};
                direct_[idx] = vec3(0.0f);
                const GBufferPixel& g = gbuf_[idx];
                if (!g.valid || luminance(g.Le) > 0.0f) continue; // 光源表面无需间接

                pcg32 rng_path(cfg.seed, (uint64_t)idx * 2 + 0);

                // 直接光照：spp 个样本平均（与 PT 每顶点 1 NEE 样本同口径）
                vec3 direct(0.0f);
                for (int s = 0; s < cfg.spp; ++s)
                    direct += direct_lighting(scene, g.x, g.n, g.albedo, rng_path, stats);
                direct_[idx] = direct / (float)cfg.spp;

                // 初始候选：x0 --BSDF--> x1 --NEE--> l（每像素 1 条，Ouyang 2021 §3.1）
                // 注意：ReSTIR 的 spp 口径与 PT 不同 —— spp 仅缩放直接光照采样数；
                // 间接路径始终为每像素 1 条（空间复用负责降低方差）。
                {
                    PathSample z;
                    z.valid = 0; z.suffix = vec3(0.0f); z.pdf_light = 1.0f;
                    float p_hat = 0.0f, w = 0.0f;

                    float pdf_w;
                    vec3  wi = cosine_sample_hemisphere(g.n, rng_path, pdf_w);
                    float cos0 = std::max(0.0f, glm::dot(g.n, wi));
                    if (pdf_w > 0.0f && cos0 > 0.0f) {
                        stats.gi_rays++;
                        Hit h1;
                        Ray r1{g.x + g.n * RAY_EPS, wi};
                        if (scene.intersect(r1, h1)) {
                            const Material& m1 = scene.materials[h1.material_id];
                            z.x1 = h1.p; z.n1 = h1.n;
                            // 若 x1 落在光源上，该方向属于 x0 的**直接光照**（已由 direct_ 的 NEE 覆盖），
                            // 不构成间接路径，直接丢弃，避免与 direct_ 重复计数。
                            if (luminance(m1.emission) <= 0.0f) {
                                // suffix = NEE(x1)：x1 处反射出射辐亮度的无偏估计
                                //（不含 x1 自身自发光，理由同上）
                                LightSample ls = sample_light(scene, rng_path);
                                z.pdf_light = ls.pdf;
                                z.suffix = light_contribution(scene, h1.p, h1.n, m1.albedo, ls, stats);
                                z.valid = 1;
                            }

                            float cos1 = std::max(0.0f, glm::dot(h1.n, -wi));
                            float dist2 = glm::dot(h1.p - g.x, h1.p - g.x);
                            vec3  f = (g.albedo * INV_PI) * cos0 * cos1 / dist2 * z.suffix;
                            p_hat = luminance(f);
                            // q(z) = p_A(x1) = p_ω(ω1)·cos1/dist²。
                            // 样本 z 视为 x1（光采样 l 已作为 suffix 的随机部分），
                            // suffix 内部的 1/p_A(l) 是 NEE 估计的一部分，q 不得再含 ls.pdf，
                            // 否则光源 pdf 被重复计入（间接光偏暗 p_A(l) 倍）。
                            float q = pdf_w * cos1 / dist2;
                            if (q > 0.0f && p_hat > 0.0f) w = p_hat / q;
                        }
                    }
                    reservoir_update(R, z, w, p_hat, rng_path);
                }
                reservoir_finalize(R);
            }
        }
    });
}

// ------------------------- Pass 3: Spatial Reuse -----------------------------
void ReSTIRRenderer::pass_spatial_reuse(const Scene& scene, const RenderConfig& cfg, Stats& stats) {
    parallel_rows(H_, cfg.threads, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < W_; ++x) {
                int idx = y * W_ + x;
                Reservoir& C = reused_[idx];
                C = Reservoir{};
                const GBufferPixel& g = gbuf_[idx];
                if (!g.valid || luminance(g.Le) > 0.0f) continue;

                pcg32 rng_sel(cfg.seed ^ 0x9E3779B9ULL, (uint64_t)idx * 2 + 1); // 邻居选择独立流

                // 合并自身 initial reservoir（p̂ 已知，无需重估）
                const Reservoir& own = reservoirs_[idx];
                if (own.M > 0 && own.p_hat_y > 0.0f)
                    reservoir_merge(C, own, own.p_hat_y, cfg.m_cap, rng_sel);
                else { C.w_sum += own.w_sum; C.M += own.M; }

                // 随机邻居：半径 r 方形邻域
                for (int k = 0; k < cfg.candidates; ++k) {
                    int nx = x + (int)rng_sel.next_u32_bounded(2u * cfg.spatial_radius + 1u) - cfg.spatial_radius;
                    int ny = y + (int)rng_sel.next_u32_bounded(2u * cfg.spatial_radius + 1u) - cfg.spatial_radius;
                    if (nx < 0) nx = 0; if (nx >= W_) nx = W_ - 1;
                    if (ny < 0) ny = 0; if (ny >= H_) ny = H_ - 1;
                    const Reservoir& r = reservoirs_[ny * W_ + nx];
                    if (r.M <= 0) continue; // 无候选（光源像素）
                    // 关键：即使邻居 reservoir 无效（W=0 / p̂=0），其 M 也必须并入 M_c，
                    // 否则候选总数被少计、W 虚高 → 整体偏亮（GRIS 候选计数不变量）。
                    float p_hat_q = 0.0f;
                    if (r.p_hat_y > 0.0f && r.W > 0.0f) {
                        // 重连接：评估邻居样本在当前像素的目标函数
                        ShiftResult sr = evaluate_reconnect(scene, g, r.y, !cfg.biased, stats);
                        p_hat_q = sr.p_hat;
                    }
                    reservoir_merge(C, r, p_hat_q, cfg.m_cap, rng_sel);
                }
                reservoir_finalize(C);
            }
        }
    });
}

// ------------------------------ Pass 4: Shading ------------------------------
void ReSTIRRenderer::pass_shade(const Scene& scene, const RenderConfig& cfg, Image& img, Stats& stats) {
    parallel_rows(H_, cfg.threads, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < W_; ++x) {
                int idx = y * W_ + x;
                const GBufferPixel& g = gbuf_[idx];
                if (!g.valid) { img.at(x, y) = scene.env_radiance; continue; }
                if (luminance(g.Le) > 0.0f) { img.at(x, y) = g.Le; continue; }

                const Reservoir& R = cfg.reuse_spatial ? reused_[idx] : reservoirs_[idx];
                vec3 indirect(0.0f);
                if (R.W > 0.0f && R.y.valid) {
                    // f_q(y)：重估前缀（无可见性 —— unbiased 已在合并时过滤）
                    vec3 dir = R.y.x1 - g.x;
                    float dist2 = glm::dot(dir, dir);
                    if (dist2 > 4.0f * RAY_EPS * RAY_EPS) {
                        float dist = std::sqrt(dist2);
                        vec3 wi = dir / dist;
                        float cos0 = std::max(0.0f, glm::dot(g.n, wi));
                        float cos1 = std::max(0.0f, glm::dot(R.y.n1, -wi));
                        vec3 f = (g.albedo * INV_PI) * cos0 * cos1 / dist2 * R.y.suffix;
                        indirect = f * R.W;
                    }
                }
                img.at(x, y) = g.Le + direct_[idx] + indirect;
            }
        }
    });
}

Image ReSTIRRenderer::render(const Scene& scene, const Camera& cam, const RenderConfig& cfg, Stats& stats) {
    W_ = cfg.width; H_ = cfg.height;
    gbuf_.resize((size_t)W_ * H_);
    reservoirs_.resize((size_t)W_ * H_);
    reused_.resize((size_t)W_ * H_);
    direct_.resize((size_t)W_ * H_);

    pass_gbuffer(scene, cam, cfg, stats);
    pass_initial(scene, cfg, stats);
    if (cfg.reuse_spatial) pass_spatial_reuse(scene, cfg, stats);
    Image img(W_, H_);
    pass_shade(scene, cfg, img, stats);

    // Reservoir 诊断（单线程，仅统计；对应 AGENT.md §7.2 E5 权重异常值监测）
    wstats_ = WStats{};
    const std::vector<Reservoir>& use = cfg.reuse_spatial ? reused_ : reservoirs_;
    for (int idx = 0; idx < W_ * H_; ++idx) {
        const GBufferPixel& g = gbuf_[idx];
        if (!g.valid || luminance(g.Le) > 0.0f) continue;
        const Reservoir& R = use[idx];
        wstats_.w_mean += R.W;
        if ((double)R.W > wstats_.w_max) wstats_.w_max = R.W;
        if (R.W <= 0.0f) wstats_.zero_frac += 1.0;
        if (R.M > wstats_.m_max) wstats_.m_max = R.M;
        ++wstats_.n_pixels;
    }
    if (wstats_.n_pixels > 0) {
        wstats_.w_mean    /= (double)wstats_.n_pixels;
        wstats_.zero_frac /= (double)wstats_.n_pixels;
    }
    return img;
}

} // namespace restir
