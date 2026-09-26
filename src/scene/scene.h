#pragma once
// =============================================================================
// scene.h — 场景数据结构 + 自写 BVH 加速结构
//
// 选型说明（docs/00-selection.md §2）：BVH 采用分桶 SAH 划分 + 深度优先遍历，
// 消除外部依赖（Embree 预留 CMake option USE_EMBREE 作为升级路径）。
// =============================================================================
#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <vector>
#include <string>
#include "core/camera.h"
#include "core/math_utils.h"

namespace restir {

struct Material {
    vec3 albedo{0.5f};     // Lambertian 反照率
    vec3 emission{0.0f};   // 自发光（>0 即面光源）
};

struct Triangle {
    vec3 v0, v1, v2;
    int  material_id = 0;
};

struct Hit {
    float t     = 1e30f;
    vec3  p;
    vec3  n;          // 面向入射方向的几何法线
    int   tri_id      = -1;
    int   material_id = -1;
};

// 面光源：引用发光三角形；采样时按面积加权选择
struct AreaLight {
    int   tri_id = -1;
    float area   = 0.0f;
};

// 点光源（可选；与面光源互斥使用，见 docs/01-design.md §3 边界说明）
struct PointLight {
    bool  enabled = false;
    vec3  pos{0.0f};
    vec3  intensity{0.0f}; // 辐射强度 I（W/sr）
};

struct BVHNode {
    vec3 bmin, bmax;
    int  left  = -1;  // 内部节点：左子索引；叶子：-1
    int  right = -1;  // 内部节点：右子索引
    int  start = 0;   // 叶子：prim_indices 起始
    int  count = 0;   // 叶子：图元数（>0 表示叶子）
};

struct Scene {
    std::vector<Triangle>  triangles;
    std::vector<Material>  materials;
    std::vector<AreaLight> lights;
    PointLight             point_light;
    vec3                   env_radiance{0.0f};
    float                  total_light_area = 0.0f;
    std::vector<float>     light_cdf; // 面积加权 CDF，用于光源选择

    // BVH 数据
    std::vector<BVHNode> nodes;
    std::vector<int>     prim_indices;

    void build_bvh();
    void finalize(); // 构建光源采样表 + BVH

    bool intersect(const Ray& ray, Hit& hit) const;
    bool occluded(const Ray& ray) const; // 检测 [tmin,tmax] 内任意遮挡

    // 两点间可见性（端点沿法线偏移 RAY_EPS，调用方负责偏移）
    bool visible(const vec3& a, const vec3& b) const;

    int  triangle_count() const { return (int)triangles.size(); }
};

// ---------------- 程序化测试场景（docs/01-design.md §6） ----------------
// name: "cornell" (S1) / "occlusion" (S2) / "hdr" (S3)
// tri_budget: E6 复杂度实验用，场景内重复几何体达到目标三角形数量级（仅 cornell 支持）
Scene build_scene(const std::string& name, int tri_budget = 0);
Camera build_camera(const std::string& name, int width, int height);

} // namespace restir
