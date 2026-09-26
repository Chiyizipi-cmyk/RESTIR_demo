// =============================================================================
// main.cpp — CLI 入口
//
// 用法：
//   restir-gi --scene cornell --method restir --spp 4 --seed 42 --output results/x
// 输出：
//   <output>.{exr,png,pfm} + <output>.json（配置 + 射线分类统计 + 计时）
// =============================================================================
#include "scene/scene.h"
#include "renderer/pathtracer.h"
#include "renderer/restir_renderer.h"
#include "io/image.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using namespace restir;

namespace {

void print_usage() {
    std::printf(
        "restir-gi — ReSTIR GI offline renderer (course A6)\n"
        "  --scene {cornell|occlusion|hdr}   default cornell\n"
        "  --method {pt|restir}              default restir\n"
        "  --tri-budget <int>                E6: cornell grid fill triangle target\n"
        "  --width/--height <int>            default 128\n"
        "  --spp <int>                       default 1\n"
        "  --max-depth <int>                 default 4 (PT)\n"
        "  --reuse-spatial {0|1}             default 1\n"
        "  --biased {0|1}                    default 1\n"
        "  --spatial-radius <int>            default 2\n"
        "  --candidates-per-pixel <int>      default 5\n"
        "  --m-cap <int>                     default 30\n"
        "  --seed <uint64>                   default 42\n"
        "  --threads <int>                   default 0 (hardware)\n"
        "  --output <prefix>                 default results/out\n"
        "  --no-exr / --no-png / --no-pfm    关闭对应输出\n");
}

template <typename T>
T parse_num(const char* s);

} // namespace

int main(int argc, char** argv) {
    RenderConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if      (a == "--scene")      cfg.scene = next();
        else if (a == "--method")     cfg.method = next();
        else if (a == "--tri-budget") cfg.tri_budget = std::stoi(next());
        else if (a == "--width")      cfg.width = std::stoi(next());
        else if (a == "--height")     cfg.height = std::stoi(next());
        else if (a == "--spp")        cfg.spp = std::stoi(next());
        else if (a == "--max-depth")  cfg.max_depth = std::stoi(next());
        else if (a == "--reuse-spatial") cfg.reuse_spatial = std::stoi(next()) != 0;
        else if (a == "--biased")        cfg.biased = std::stoi(next()) != 0;
        else if (a == "--spatial-radius")       cfg.spatial_radius = std::stoi(next());
        else if (a == "--candidates-per-pixel") cfg.candidates = std::stoi(next());
        else if (a == "--m-cap")     cfg.m_cap = std::stoi(next());
        else if (a == "--seed")      cfg.seed = std::stoull(next());
        else if (a == "--threads")   cfg.threads = std::stoi(next());
        else if (a == "--output")    cfg.output = next();
        else if (a == "--no-exr")    cfg.write_exr = false;
        else if (a == "--no-png")    cfg.write_png = false;
        else if (a == "--no-pfm")    cfg.write_pfm = false;
        else if (a == "--help" || a == "-h") { print_usage(); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); print_usage(); return 2; }
    }

    // 场景与相机
    Scene  scene = build_scene(cfg.scene, cfg.tri_budget);
    Camera cam   = build_camera(cfg.scene, cfg.width, cfg.height);

    int threads = cfg.threads > 0 ? cfg.threads : (int)std::thread::hardware_concurrency();

    // 渲染（纯渲染计时，不含场景构建与 I/O）
    Stats stats;
    auto t0 = std::chrono::steady_clock::now();
    Image img;
    if (cfg.method == "pt") {
        PathTracer pt;
        img = pt.render(scene, cam, cfg, stats);
    } else {
        ReSTIRRenderer re;
        img = re.render(scene, cam, cfg, stats);
    }
    auto t1 = std::chrono::steady_clock::now();
    double render_sec = std::chrono::duration<double>(t1 - t0).count();

    // 输出
    auto t2 = std::chrono::steady_clock::now();
    if (cfg.write_exr) write_exr(img, cfg.output + ".exr");
    if (cfg.write_png) write_png(img, cfg.output + ".png");
    if (cfg.write_pfm) write_pfm(img, cfg.output + ".pfm");
    auto t3 = std::chrono::steady_clock::now();
    double io_sec = std::chrono::duration<double>(t3 - t2).count();

    uint64_t total_rays = stats.total_rays();
    double   px         = (double)cfg.width * cfg.height;

    // JSON（配置回显 + 分类射线 + 计时）
    std::string js = cfg.output + ".json";
    FILE* f = std::fopen(js.c_str(), "w");
    if (f) {
        std::fprintf(f,
            "{\n"
            "  \"scene\": \"%s\", \"method\": \"%s\", \"tri_budget\": %d,\n"
            "  \"width\": %d, \"height\": %d, \"spp\": %d, \"max_depth\": %d,\n"
            "  \"reuse_spatial\": %d, \"biased\": %d, \"spatial_radius\": %d,\n"
            "  \"candidates_per_pixel\": %d, \"m_cap\": %d,\n"
            "  \"seed\": %llu, \"threads\": %d, \"triangles\": %d,\n"
            "  \"primary_rays\": %llu, \"gi_rays\": %llu,\n"
            "  \"shadow_nee_rays\": %llu, \"shadow_reuse_rays\": %llu,\n"
            "  \"total_rays\": %llu, \"rays_per_pixel\": %.4f,\n"
            "  \"time_render_sec\": %.6f, \"time_io_sec\": %.6f\n"
            "}\n",
            cfg.scene.c_str(), cfg.method.c_str(), cfg.tri_budget,
            cfg.width, cfg.height, cfg.spp, cfg.max_depth,
            (int)cfg.reuse_spatial, (int)cfg.biased, cfg.spatial_radius,
            cfg.candidates, cfg.m_cap,
            (unsigned long long)cfg.seed, threads, scene.triangle_count(),
            (unsigned long long)stats.primary_rays.load(),
            (unsigned long long)stats.gi_rays.load(),
            (unsigned long long)stats.shadow_nee.load(),
            (unsigned long long)stats.shadow_reuse.load(),
            (unsigned long long)total_rays, total_rays / px,
            render_sec, io_sec);
        std::fclose(f);
    }
    std::printf("done: %s | rays=%llu (%.2f/px) | render=%.3fs io=%.3fs\n",
                cfg.output.c_str(), (unsigned long long)total_rays, total_rays / px,
                render_sec, io_sec);
    return 0;
}
