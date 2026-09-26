#pragma once
// =============================================================================
// image.h — 线性 HDR 浮点图像 + EXR / PNG / PFM 输出
//
//   EXR : tinyexr（HDR 存档格式）
//   PNG : stb_image_write（Reinhard tonemap + gamma 2.2 预览）
//   PFM : 无损 float32，供 tools/ Python 脚本计算 MSE/PSNR
// =============================================================================
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "core/math_utils.h"

namespace restir {

struct Image {
    int width = 0, height = 0;
    std::vector<vec3> pixels;

    Image() = default;
    Image(int w, int h) : width(w), height(h), pixels((size_t)w * h, vec3(0.0f)) {}

    vec3&       at(int x, int y)       { return pixels[(size_t)y * width + x]; }
    const vec3& at(int x, int y) const { return pixels[(size_t)y * width + x]; }
};

bool write_exr(const Image& img, const std::string& path);
bool write_png(const Image& img, const std::string& path);
bool write_pfm(const Image& img, const std::string& path);

} // namespace restir
