// 渲染冒烟测试：Cornell Box 16×16
//  1. PT 与 ReSTIR 所有像素有限且非负
//  2. 确定性：同种子两次渲染逐位一致
//  3. ReSTIR 像素均值与 PT 同量级（无灾难性偏差）
#include "tests.h"
#include "renderer/pathtracer.h"
#include "renderer/restir_renderer.h"
#include <cmath>

namespace restir::test {

void test_render_sanity(Context& ctx) {
    Scene scene = build_scene("cornell");
    Camera cam = build_camera("cornell", 16, 16);

    RenderConfig cfg;
    cfg.width = 16; cfg.height = 16; cfg.spp = 4; cfg.seed = 42;
    cfg.write_exr = cfg.write_png = cfg.write_pfm = false;

    // PT
    Stats st1;
    PathTracer pt;
    cfg.method = "pt";
    Image img_pt = pt.render(scene, cam, cfg, st1);

    // ReSTIR
    Stats st2;
    ReSTIRRenderer re;
    cfg.method = "restir"; cfg.reuse_spatial = true; cfg.biased = false;
    Image img_re = re.render(scene, cam, cfg, st2);

    auto finite_nonneg = [](const Image& im) {
        for (auto& p : im.pixels)
            for (int c = 0; c < 3; ++c)
                if (!std::isfinite(p[c]) || p[c] < 0.0f) return false;
        return true;
    };
    CHECK_TRUE(ctx, finite_nonneg(img_pt));
    CHECK_TRUE(ctx, finite_nonneg(img_re));

    double mean_pt = 0, mean_re = 0;
    for (auto& p : img_pt.pixels) mean_pt += luminance(p);
    for (auto& p : img_re.pixels) mean_re += luminance(p);
    mean_pt /= img_pt.pixels.size();
    mean_re /= img_re.pixels.size();
    CHECK_TRUE(ctx, mean_pt > 0.0);
    CHECK_TRUE(ctx, mean_re > 0.0);
    // 均值同量级（容差 50%：4spp 下统计涨落较大）
    CHECK_NEAR(ctx, mean_re, mean_pt, 0.5);

    // 确定性：同种子重跑逐位一致
    Stats st3;
    ReSTIRRenderer re2;
    Image img_re2 = re2.render(scene, cam, cfg, st3);
    bool identical = true;
    for (size_t i = 0; i < img_re.pixels.size(); ++i)
        identical &= (img_re.pixels[i] == img_re2.pixels[i]);
    CHECK_TRUE(ctx, identical);

    // 射线计数：ReSTIR 分类计数非零（primary + gi + nee + reuse）
    CHECK_TRUE(ctx, st2.primary_rays > 0);
    CHECK_TRUE(ctx, st2.gi_rays > 0);
    CHECK_TRUE(ctx, st2.shadow_nee > 0);
    CHECK_TRUE(ctx, st2.shadow_reuse > 0); // unbiased 空间复用必有重连可见性检测
}

} // namespace restir::test
