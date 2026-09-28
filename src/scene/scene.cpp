// =============================================================================
// scene.cpp — BVH 构建（分桶 SAH）与求交（Möller–Trumbore + 深度优先遍历）
// =============================================================================
#include "scene/scene.h"
#include <algorithm>
#include <cmath>

namespace restir {

// ------------------------------- 求交 ---------------------------------------

static inline bool ray_tri(const Ray& ray, const Triangle& tri, float& t_out, float& u_out, float& v_out) {
    // Möller–Trumbore
    vec3 e1 = tri.v1 - tri.v0;
    vec3 e2 = tri.v2 - tri.v0;
    vec3 p  = glm::cross(ray.d, e2);
    float det = glm::dot(e1, p);
    if (std::fabs(det) < 1e-12f) return false;
    float inv = 1.0f / det;
    vec3  tv  = ray.o - tri.v0;
    float u   = glm::dot(tv, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    vec3  qv  = glm::cross(tv, e1);
    float v   = glm::dot(ray.d, qv) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    float t   = glm::dot(e2, qv) * inv;
    if (t < ray.tmin || t > ray.tmax) return false;
    t_out = t; u_out = u; v_out = v;
    return true;
}

static inline bool ray_aabb(const Ray& ray, const vec3& bmin, const vec3& bmax, float& t_near) {
    vec3 inv_d = 1.0f / ray.d;
    vec3 t0    = (bmin - ray.o) * inv_d;
    vec3 t1    = (bmax - ray.o) * inv_d;
    vec3 tmin3 = glm::min(t0, t1);
    vec3 tmax3 = glm::max(t0, t1);
    float tmin = std::max(std::max(tmin3.x, tmin3.y), std::max(tmin3.z, ray.tmin));
    float tmax = std::min(std::min(tmax3.x, tmax3.y), std::min(tmax3.z, ray.tmax));
    t_near = tmin;
    return tmax >= tmin;
}

// ------------------------------- BVH 构建 ------------------------------------

namespace {

struct BuildPrim {
    int  tri_id;
    vec3 bmin, bmax, centroid;
};

constexpr int SAH_BINS   = 12;
constexpr int LEAF_MAX   = 4;

int build_recursive(std::vector<BVHNode>& nodes, std::vector<int>& indices,
                    std::vector<BuildPrim>& prims, int start, int count) {
    BVHNode node;
    // 包围盒
    vec3 bmin(1e30f), bmax(-1e30f);
    for (int i = start; i < start + count; ++i) {
        const BuildPrim& p = prims[indices[i]];
        bmin = glm::min(bmin, p.bmin);
        bmax = glm::max(bmax, p.bmax);
    }
    node.bmin = bmin; node.bmax = bmax;

    if (count <= LEAF_MAX) {
        node.start = start; node.count = count;
        nodes.push_back(node);
        return (int)nodes.size() - 1;
    }

    // 分桶 SAH：沿最长轴划分
    vec3 extent = bmax - bmin;
    int axis = 0;
    if (extent.y > extent.x && extent.y >= extent.z) axis = 1;
    else if (extent.z > extent.x && extent.z >= extent.y) axis = 2;

    float axis_min = bmin[axis], axis_len = extent[axis];
    if (axis_len < 1e-9f) { // 退化：直接作叶子
        node.start = start; node.count = count;
        nodes.push_back(node);
        return (int)nodes.size() - 1;
    }

    struct Bin { int count = 0; vec3 bmin{1e30f}, bmax{-1e30f}; };
    Bin bins[SAH_BINS];
    for (int i = start; i < start + count; ++i) {
        const BuildPrim& p = prims[indices[i]];
        int b = std::min(SAH_BINS - 1, (int)((p.centroid[axis] - axis_min) / axis_len * SAH_BINS));
        bins[b].count++;
        bins[b].bmin = glm::min(bins[b].bmin, p.bmin);
        bins[b].bmax = glm::max(bins[b].bmax, p.bmax);
    }

    // 扫描最优分割
    float best_cost = 1e30f;
    int   best_bin  = -1;
    auto  area_of = [](const vec3& mn, const vec3& mx) {
        vec3 e = mx - mn;
        return e.x * e.y + e.y * e.z + e.z * e.x;
    };
    int   left_count = 0;
    vec3  lb_min(1e30f), lb_max(-1e30f);
    // 前缀扫描
    int   counts[SAH_BINS]; vec3 cmins[SAH_BINS]; vec3 cmaxs[SAH_BINS];
    for (int i = 0; i < SAH_BINS; ++i) {
        left_count += bins[i].count;
        lb_min = glm::min(lb_min, bins[i].bmin);
        lb_max = glm::max(lb_max, bins[i].bmax);
        counts[i] = left_count; cmins[i] = lb_min; cmaxs[i] = lb_max;
    }
    int   right_count = 0;
    vec3  rb_min(1e30f), rb_max(-1e30f);
    for (int i = SAH_BINS - 1; i >= 1; --i) {
        right_count += bins[i].count;
        rb_min = glm::min(rb_min, bins[i].bmin);
        rb_max = glm::max(rb_max, bins[i].bmax);
        if (counts[i - 1] > 0 && right_count > 0) {
            float cost = area_of(cmins[i - 1], cmaxs[i - 1]) * (float)counts[i - 1]
                       + area_of(rb_min, rb_max) * (float)right_count;
            if (cost < best_cost) { best_cost = cost; best_bin = i; }
        }
    }

    if (best_bin < 0) { // 无法划分（所有质心重合）
        node.start = start; node.count = count;
        nodes.push_back(node);
        return (int)nodes.size() - 1;
    }

    // 分区（稳定划分，确定性）
    auto mid = std::stable_partition(indices.begin() + start, indices.begin() + start + count,
        [&](int id) {
            const BuildPrim& p = prims[id];
            int b = std::min(SAH_BINS - 1, (int)((p.centroid[axis] - axis_min) / axis_len * SAH_BINS));
            return b < best_bin;
        });
    int left_n = (int)(mid - (indices.begin() + start));
    if (left_n == 0 || left_n == count) {
        node.start = start; node.count = count;
        nodes.push_back(node);
        return (int)nodes.size() - 1;
    }

    // 预留当前节点位置，递归构建子树
    nodes.push_back(node);
    int my_index = (int)nodes.size() - 1;
    int left_idx  = build_recursive(nodes, indices, prims, start, left_n);
    int right_idx = build_recursive(nodes, indices, prims, start + left_n, count - left_n);
    nodes[my_index].left  = left_idx;
    nodes[my_index].right = right_idx;
    nodes[my_index].count = 0;
    return my_index;
}

} // anonymous namespace

void Scene::build_bvh() {
    std::vector<BuildPrim> prims(triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i) {
        const Triangle& t = triangles[i];
        BuildPrim& p = prims[i];
        p.tri_id   = (int)i;
        p.bmin     = glm::min(t.v0, glm::min(t.v1, t.v2));
        p.bmax     = glm::max(t.v0, glm::max(t.v1, t.v2));
        // 防止零厚度包围盒
        p.bmax = glm::max(p.bmax, p.bmin + vec3(1e-6f));
        p.centroid = (t.v0 + t.v1 + t.v2) * (1.0f / 3.0f);
    }
    prim_indices.resize(triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i) prim_indices[i] = (int)i;
    nodes.clear();
    if (!triangles.empty())
        build_recursive(nodes, prim_indices, prims, 0, (int)triangles.size());
}

void Scene::finalize() {
    // 收集发光三角形为面光源，构建面积加权 CDF
    lights.clear();
    light_cdf.clear();
    total_light_area = 0.0f;
    for (size_t i = 0; i < triangles.size(); ++i) {
        const Material& m = materials[triangles[i].material_id];
        if (luminance(m.emission) > 0.0f) {
            float a = tri_area(triangles[i].v0, triangles[i].v1, triangles[i].v2);
            lights.push_back(AreaLight{(int)i, a});
            total_light_area += a;
            light_cdf.push_back(total_light_area);
        }
    }
    build_bvh();
}

// ------------------------------- 遍历 ---------------------------------------

bool Scene::intersect(const Ray& ray, Hit& hit) const {
    bool found = false;
    if (nodes.empty()) return false;
    Ray r = ray;
    int   stack[64];
    int   sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const BVHNode& node = nodes[stack[--sp]];
        float t_near;
        if (!ray_aabb(r, node.bmin, node.bmax, t_near) || t_near > r.tmax) continue;
        if (node.count > 0) {
            for (int i = node.start; i < node.start + node.count; ++i) {
                int tid = prim_indices[i];
                float t, u, v;
                if (ray_tri(r, triangles[tid], t, u, v)) {
                    r.tmax = t;
                    found  = true;
                    hit.t  = t;
                    hit.tri_id = tid;
                    hit.material_id = triangles[tid].material_id;
                    hit.p = ray.o + ray.d * t;
                    vec3 n = glm::normalize(glm::cross(triangles[tid].v1 - triangles[tid].v0,
                                                       triangles[tid].v2 - triangles[tid].v0));
                    hit.geo_n = n;
                    // 正面判定必须在翻转前完成：dot(geo_n, d) < 0 表示从发光面正面看过去
                    hit.front = glm::dot(n, ray.d) < 0.0f;
                    // 着色法线面向入射侧
                    hit.n = hit.front ? n : -n;
                }
            }
        } else {
            stack[sp++] = node.left;
            stack[sp++] = node.right;
        }
    }
    return found;
}

bool Scene::occluded(const Ray& ray) const {
    if (nodes.empty()) return false;
    int stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const BVHNode& node = nodes[stack[--sp]];
        float t_near;
        if (!ray_aabb(ray, node.bmin, node.bmax, t_near) || t_near > ray.tmax) continue;
        if (node.count > 0) {
            for (int i = node.start; i < node.start + node.count; ++i) {
                float t, u, v;
                if (ray_tri(ray, triangles[prim_indices[i]], t, u, v)) return true;
            }
        } else {
            stack[sp++] = node.left;
            stack[sp++] = node.right;
        }
    }
    return false;
}

bool Scene::visible(const vec3& a, const vec3& b) const {
    vec3  d    = b - a;
    float dist = glm::length(d);
    if (dist < 2.0f * RAY_EPS) return true;
    Ray shadow{a, d / dist, RAY_EPS, dist - 2.0f * RAY_EPS};
    return !occluded(shadow);
}

} // namespace restir
