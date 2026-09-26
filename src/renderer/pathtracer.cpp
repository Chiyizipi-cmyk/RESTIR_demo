#include "renderer/pathtracer.h"
#include "renderer/lighting.h"
#include "core/math_utils.h"
#include <thread>
#include <vector>
#include <cmath>

namespace restir {

// 单条路径的辐射亮度估计（NEE-PT）
static vec3 trace_path(const Scene& scene, Ray ray, int max_depth, pcg32& rng, Stats& stats) {
    vec3 throughput(1.0f);
    vec3 L(0.0f);

    for (int depth = 0; depth < max_depth; ++depth) {
        Hit hit;
        if (depth == 0) stats.primary_rays++;
        else            stats.gi_rays++;

        if (!scene.intersect(ray, hit)) {
            L += throughput * scene.env_radiance;
            break;
        }
        const Material& mat = scene.materials[hit.material_id];
        if (glm::dot(mat.emission, mat.emission) > 0.0f) {
            L += throughput * mat.emission; // 自发光面片
            break; // 到达光源，停止路径
        }

        // NEE：每个顶点 1 个光源样本
        L += throughput * direct_lighting(scene, hit.p, hit.n, mat.albedo, rng, stats);

        if (depth == max_depth - 1) break; // 达到最大深度

        // 间接方向：余弦加权半球采样
        float pdf_w;
        vec3  wi = cosine_sample_hemisphere(hit.n, rng, pdf_w);
        if (pdf_w <= 0.0f) break;
        vec3 brdf = mat.albedo * INV_PI;
        float cos_t = std::max(0.0f, glm::dot(hit.n, wi));
        throughput *= brdf * cos_t / pdf_w;
        ray = Ray{hit.p + hit.n * RAY_EPS, wi};
    }
    return L;
}

Image PathTracer::render(const Scene& scene, const Camera& cam, const RenderConfig& cfg, Stats& stats) const {
    Image img(cfg.width, cfg.height);
    int W = cfg.width, H = cfg.height;

    int num_threads = cfg.threads > 0 ? cfg.threads : (int)std::thread::hardware_concurrency();
    if (num_threads < 1) num_threads = 1;

    // 静态行块划分：第 t 线程处理 [t*chunk, min((t+1)*chunk, H)) 行
    int chunk = (H + num_threads - 1) / num_threads;

    auto worker = [&](int t) {
        int y0 = t * chunk;
        int y1 = std::min(y0 + chunk, H);
        if (y0 >= y1) return;
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < W; ++x) {
                uint64_t pixel_idx = (uint64_t)(y * W + x);
                pcg32 rng(cfg.seed, pixel_idx * 2 + 0); // 路径流
                vec3 color(0.0f);
                for (int s = 0; s < cfg.spp; ++s) {
                    Ray ray = cam.generate_ray_center(x, y); // 中心 ray，与 ReSTIR 公平对比
                    color += trace_path(scene, ray, cfg.max_depth, rng, stats);
                }
                img.at(x, y) = color / (float)cfg.spp;
            }
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(num_threads);
    for (int t = 0; t < num_threads; ++t) pool.emplace_back(worker, t);
    for (auto& th : pool) th.join();

    return img;
}

} // namespace restir
