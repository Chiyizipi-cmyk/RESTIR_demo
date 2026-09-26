// =============================================================================
// image.cpp — EXR / PNG / PFM 写出实现
// =============================================================================
#include "io/image.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 1
#include "tinyexr.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace restir {

bool write_exr(const Image& img, const std::string& path) {
    // EXR 约定首行为图像顶行；内部缓冲 y=0 为底行 → 翻转
    std::vector<vec3> flipped(img.pixels.size());
    for (int y = 0; y < img.height; ++y)
        std::copy_n(&img.pixels[(size_t)(img.height - 1 - y) * img.width], img.width,
                    &flipped[(size_t)y * img.width]);
    const char* err = nullptr;
    int ret = SaveEXR(reinterpret_cast<const float*>(flipped.data()),
                      img.width, img.height, 3 /*RGB*/, 1 /*float*/, path.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        if (err) { std::fprintf(stderr, "EXR error: %s\n", err); FreeEXRErrorMessage(err); }
        return false;
    }
    return true;
}

bool write_png(const Image& img, const std::string& path) {
    // stb 约定首行为图像顶行；内部缓冲 y=0 为底行 → 翻转
    std::vector<unsigned char> bytes((size_t)img.width * img.height * 3);
    for (int y = 0; y < img.height; ++y) {
        int sy = img.height - 1 - y;
        for (int x = 0; x < img.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                float v = img.pixels[(size_t)sy * img.width + x][c];
                v = std::max(0.0f, v);
                v = v / (1.0f + v);                    // Reinhard tonemap
                v = std::pow(v, 1.0f / 2.2f);          // gamma
                bytes[((size_t)y * img.width + x) * 3 + c] =
                    (unsigned char)std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f);
            }
        }
    }
    return stbi_write_png(path.c_str(), img.width, img.height, 3, bytes.data(),
                          img.width * 3) != 0;
}

// PFM：scale = -1.0（小端）；本项目写入为首行 = 图像顶行（与 EXR/PNG 一致，
// tools/pfm 读取端按同约定解析，无需 flipud）。
bool write_pfm(const Image& img, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "PF\n%d %d\n-1.0\n", img.width, img.height);
    for (int y = img.height - 1; y >= 0; --y)
        std::fwrite(&img.pixels[(size_t)y * img.width], sizeof(vec3), img.width, f);
    std::fclose(f);
    return true;
}

} // namespace restir
