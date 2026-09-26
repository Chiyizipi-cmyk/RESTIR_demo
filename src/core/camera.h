#pragma once
// =============================================================================
// camera.h — 针孔相机
// =============================================================================
#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <cmath>
#include "core/rng.h"
#include "core/math_utils.h"

namespace restir {

struct Ray {
    vec3  o;
    vec3  d;
    float tmin = RAY_EPS;
    float tmax = 1e30f;
};

struct Camera {
    vec3  origin;
    vec3  lower_left;
    vec3  horizontal;
    vec3  vertical;
    int   width  = 128;
    int   height = 128;

    Camera() = default;
    Camera(vec3 lookfrom, vec3 lookat, vec3 vup, float vfov_deg, int w, int h)
        : width(w), height(h) {
        float aspect = (float)w / (float)h;
        float theta  = vfov_deg * PI / 180.0f;
        float half_h = std::tan(theta * 0.5f);
        float half_w = aspect * half_h;
        vec3  w_axis = glm::normalize(lookfrom - lookat);
        vec3  u_axis = glm::normalize(glm::cross(vup, w_axis));
        vec3  v_axis = glm::cross(w_axis, u_axis);
        origin     = lookfrom;
        horizontal = 2.0f * half_w * u_axis;
        vertical   = 2.0f * half_h * v_axis;
        lower_left = origin - half_w * u_axis - half_h * v_axis - w_axis;
    }

    // 像素 (px,py) 内均匀抖动采样（抗锯齿）；rng 为像素独立流
    Ray generate_ray(int px, int py, pcg32& rng) const {
        float u = ((float)px + rng.next_f32()) / (float)width;
        float v = ((float)py + rng.next_f32()) / (float)height;
        return Ray{origin, glm::normalize(lower_left + u * horizontal + v * vertical - origin)};
    }

    // 像素中心 ray（G-Buffer 用，保证首命中与像素索引一一对应）
    Ray generate_ray_center(int px, int py) const {
        float u = ((float)px + 0.5f) / (float)width;
        float v = ((float)py + 0.5f) / (float)height;
        return Ray{origin, glm::normalize(lower_left + u * horizontal + v * vertical - origin)};
    }
};

} // namespace restir
