#pragma once
// =============================================================================
// math_utils.h — 采样与颜色工具（Lambertian 余弦加权采样、亮度、ONB）
// =============================================================================
#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <cmath>
#include "core/rng.h"

namespace restir {

using glm::vec3;

constexpr float PI      = 3.14159265358979323846f;
constexpr float INV_PI  = 0.31830988618379067154f;
constexpr float RAY_EPS = 1e-4f;

inline float luminance(const vec3& c) {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

// 正交基（Frisvad 方法，无需分支的数值稳定版本）
inline void make_onb(const vec3& n, vec3& t, vec3& b) {
    float sign = n.z >= 0.0f ? 1.0f : -1.0f;
    float a    = -1.0f / (sign + n.z);
    float c    = n.x * n.y * a;
    t = vec3(1.0f + sign * n.x * n.x * a, sign * c, -sign * n.x);
    b = vec3(c, sign + n.y * n.y * a, -n.y);
}

// 余弦加权半球采样（Lambertian BSDF 重要性采样）
// 返回世界坐标方向；pdf = cos(theta)/PI
inline vec3 cosine_sample_hemisphere(const vec3& n, pcg32& rng, float& pdf) {
    float u1 = rng.next_f32();
    float u2 = rng.next_f32();
    float r  = std::sqrt(u1);
    float phi = 2.0f * PI * u2;
    float x  = r * std::cos(phi);
    float y  = r * std::sin(phi);
    float z  = std::sqrt(std::max(0.0f, 1.0f - u1));
    vec3 t, b;
    make_onb(n, t, b);
    vec3 dir = glm::normalize(t * x + b * y + n * z);
    pdf = z * INV_PI; // cos(theta)/PI
    return dir;
}

// 三角形面积
inline float tri_area(const vec3& a, const vec3& b, const vec3& c) {
    return 0.5f * glm::length(glm::cross(b - a, c - a));
}

// 三角形内均匀采样（面积测度），pdf = 1/area
inline vec3 sample_triangle(const vec3& a, const vec3& b, const vec3& c, pcg32& rng) {
    float u1 = rng.next_f32();
    float u2 = rng.next_f32();
    float su = std::sqrt(u1);
    return a * (1.0f - su) + b * (su * (1.0f - u2)) + c * (su * u2);
}

} // namespace restir
