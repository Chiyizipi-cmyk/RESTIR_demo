// =============================================================================
// scenes.cpp — 程序化测试场景 S1/S2/S3（docs/01-design.md §6）
//
//  S1 cornell    : Cornell Box，漫反射，顶部面光源，间接光主导
//  S2 occlusion  : 双房间 + 隔墙（仅小缝透光），暴露 biased 模式漏光
//  S3 hdr        : 大房间 + 极小高亮面光源 + 大面积弱光，暴露采样难度
//
//  所有场景几何封闭（无射线逃逸），env_radiance = 0，保证 PT 与 ReSTIR 公平对比。
// =============================================================================
#include "scene/scene.h"
#include <stdexcept>

namespace restir {

namespace {

int add_material(Scene& s, const vec3& albedo, const vec3& emission = vec3(0.0f)) {
    s.materials.push_back(Material{albedo, emission});
    return (int)s.materials.size() - 1;
}

void add_tri(Scene& s, const vec3& a, const vec3& b, const vec3& c, int mat) {
    s.triangles.push_back(Triangle{a, b, c, mat});
}

// 四边形（两个三角形），顶点逆时针
void add_quad(Scene& s, const vec3& v00, const vec3& v10, const vec3& v11, const vec3& v01, int mat) {
    add_tri(s, v00, v10, v11, mat);
    add_tri(s, v00, v11, v01, mat);
}

// 水平面光源四边形（y = 常数）。
//
// 绕序约定：几何法线指向 -y（房间内部），使 NEE 的 cos_l = max(0, -dot(n, wi))
// 对房间内的被着色点为正。**这是必须的**：若法线朝上（背离房间），
// light_contribution 的朝向项恒为 0，NEE 将静默失效（历史 bug，见 docs/progress.md 阶段 5）。
// 该绕序同时与 pathtracer 中"仅发光面正面计入 emission"的判定保持一致。
void add_ceiling_light_quad(Scene& s, float x0, float z0, float x1, float z1, float y, int mat) {
    add_quad(s, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}, {x0, y, z0}, mat);
}

// 轴对齐长方体（12 三角形）
void add_box(Scene& s, const vec3& mn, const vec3& mx, int mat) {
    vec3 c[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}};
    // 外表面 6 面
    add_quad(s, c[0], c[1], c[2], c[3], mat); // z = mn（外）
    add_quad(s, c[5], c[4], c[7], c[6], mat); // z = mx
    add_quad(s, c[4], c[0], c[3], c[7], mat); // x = mn
    add_quad(s, c[1], c[5], c[6], c[2], mat); // x = mx
    add_quad(s, c[4], c[5], c[1], c[0], mat); // y = mn（底）
    add_quad(s, c[3], c[2], c[6], c[7], mat); // y = mx（顶）
}

// 把 [x0,x1]×[z0,z1]、y=const 的面光源拆成 n 个等面积子光源（E6 光源数量维度）。
// 总面积与发光强度不变 → 期望照明不变，仅光源个数 / CDF 长度变化。
void add_ceiling_light_grid(Scene& s, float x0, float z0, float x1, float z1, float y,
                            int mat, int n) {
    if (n <= 1) {
        add_ceiling_light_quad(s, x0, z0, x1, z1, y, mat);
        return;
    }
    int gx = 1, gz = 1;
    while (gx * gz < n) { if (gx <= gz) ++gx; else ++gz; }
    float sx = (x1 - x0) / (float)gx;
    float sz = (z1 - z0) / (float)gz;
    for (int i = 0; i < gx; ++i)
        for (int j = 0; j < gz; ++j) {
            if (i * gz + j >= n) return;
            add_ceiling_light_quad(s, x0 + i * sx, z0 + j * sz,
                                   x0 + (i + 1) * sx, z0 + (j + 1) * sz, y, mat);
        }
}

// ------------------------------- S1: Cornell Box -----------------------------
Scene build_cornell(int tri_budget, int light_count) {
    Scene s;
    int white = add_material(s, vec3(0.73f, 0.73f, 0.73f));
    int red   = add_material(s, vec3(0.65f, 0.05f, 0.05f));
    int green = add_material(s, vec3(0.12f, 0.45f, 0.15f));
    int light = add_material(s, vec3(0.0f), vec3(12.0f, 12.0f, 12.0f));

    // 房间 [0,1]^3，相机从 z<0 方向看入（z=0 墙面即近端墙）
    // 地板 y=0 / 天花 y=1 / 左 x=0(红) / 右 x=1(绿) / 后墙 z=1 / 前墙 z=0（封闭）
    add_quad(s, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}, white); // 地板
    add_quad(s, {0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}, white); // 天花
    add_quad(s, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}, white); // 后墙
    // 前墙移除：经典 Cornell Box 为开口，相机从外部 z<0 看入
    // add_quad(s, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}, white); // 前墙（封闭）
    add_quad(s, {0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}, red);   // 左墙
    add_quad(s, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}, green); // 右墙

    // 顶部面光源（法线朝下，指向房间内部；light_count>1 时拆为等面积子光源）
    float l = 0.13f, c = 0.5f, y = 0.998f;
    add_ceiling_light_grid(s, c - l, c - l, c + l, c + l, y, light, light_count);

    if (tri_budget <= 0) {
        // 标准 Cornell：高盒 + 矮盒
        add_box(s, {0.55f, 0.0f, 0.20f}, {0.85f, 0.60f, 0.50f}, white); // 高盒
        add_box(s, {0.15f, 0.0f, 0.45f}, {0.45f, 0.30f, 0.75f}, white); // 矮盒
    } else {
        // E6 复杂度实验：确定性网格小盒子填充
        int target = std::max(12, tri_budget);
        int n_boxes = target / 12;
        int gx = 1, gy = 1, gz = 1;
        while (gx * gy * gz < n_boxes) {
            if (gx <= gy && gx <= gz) gx++;
            else if (gy <= gz)      gy++;
            else                    gz++;
        }
        float sx = 0.8f / gx, sy = 0.7f / gy, sz = 0.8f / gz;
        float bx = sx * 0.6f, by = sy * 0.9f, bz = sz * 0.6f;
        int placed = 0;
        for (int i = 0; i < gx && placed < n_boxes; ++i)
            for (int j = 0; j < gy && placed < n_boxes; ++j)
                for (int k = 0; k < gz && placed < n_boxes; ++k) {
                    vec3 mn(0.1f + i * sx, 0.0f + j * sy, 0.1f + k * sz);
                    add_box(s, mn, mn + vec3(bx, by, bz), white);
                    ++placed;
                }
    }
    s.finalize();
    return s;
}

// ---------------------------- S2: Occlusion Maze -----------------------------
Scene build_occlusion() {
    Scene s;
    int white = add_material(s, vec3(0.72f, 0.72f, 0.70f));
    int dark  = add_material(s, vec3(0.35f, 0.35f, 0.38f));
    int light = add_material(s, vec3(0.0f), vec3(20.0f, 18.0f, 15.0f));

    // 双房间 [0,2]×[0,1]×[0,1]，x=1 处隔墙仅留 z∈[0.85,1] 小缝
    // 外墙（封闭）
    add_quad(s, {0, 0, 0}, {2, 0, 0}, {2, 0, 1}, {0, 0, 1}, white); // 地板
    add_quad(s, {0, 1, 0}, {0, 1, 1}, {2, 1, 1}, {2, 1, 0}, white); // 天花
    add_quad(s, {0, 0, 1}, {2, 0, 1}, {2, 1, 1}, {0, 1, 1}, dark);  // 后墙
    // 前墙移除（相机从 z<0 看入）
    // add_quad(s, {0, 0, 0}, {0, 1, 0}, {2, 1, 0}, {2, 0, 0}, white); // 前墙
    add_quad(s, {0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}, white); // 左墙
    add_quad(s, {2, 0, 0}, {2, 1, 0}, {2, 1, 1}, {2, 0, 1}, white); // 右墙

    // 隔墙（双面）：z ∈ [0, 0.85]，仅留缝
    add_quad(s, {1, 0, 0}, {1, 1, 0}, {1, 1, 0.85f}, {1, 0, 0.85f}, white); // 朝左面
    add_quad(s, {1, 0, 0}, {1, 0, 0.85f}, {1, 1, 0.85f}, {1, 1, 0}, white); // 朝右面

    // 光源：左房间天花小面光（法线朝下）
    add_ceiling_light_quad(s, 0.45f, 0.40f, 0.55f, 0.60f, 0.995f, light);

    // 右房间障碍物（产生软阴影/遮挡层次）
    add_box(s, {1.45f, 0.0f, 0.35f}, {1.75f, 0.45f, 0.65f}, dark);
    s.finalize();
    return s;
}

// ------------------------------- S3: HDR Small Light -------------------------
Scene build_hdr() {
    Scene s;
    int white  = add_material(s, vec3(0.70f, 0.70f, 0.72f));
    int blue   = add_material(s, vec3(0.10f, 0.20f, 0.55f));
    int bright = add_material(s, vec3(0.0f), vec3(900.0f, 850.0f, 800.0f)); // 极小高亮
    int dim    = add_material(s, vec3(0.0f), vec3(0.35f, 0.35f, 0.40f));    // 大面积弱光

    // 大房间 [0,2]^3
    add_quad(s, {0, 0, 0}, {2, 0, 0}, {2, 0, 2}, {0, 0, 2}, white); // 地板
    add_quad(s, {0, 2, 0}, {0, 2, 2}, {2, 2, 2}, {2, 2, 0}, white); // 天花
    add_quad(s, {0, 0, 2}, {2, 0, 2}, {2, 2, 2}, {0, 2, 2}, blue);  // 后墙
    // 前墙移除（相机从 z<0 看入）
    // add_quad(s, {0, 0, 0}, {0, 2, 0}, {2, 2, 0}, {2, 0, 0}, white); // 前墙
    add_quad(s, {0, 0, 0}, {0, 0, 2}, {0, 2, 2}, {0, 2, 0}, white); // 左墙
    add_quad(s, {2, 0, 0}, {2, 2, 0}, {2, 2, 2}, {2, 0, 2}, white); // 右墙

    // 极小高亮面光源（0.04 × 0.04，法线朝下）
    float c = 1.0f, w = 0.02f, y = 1.998f;
    add_ceiling_light_quad(s, c - w, c - w, c + w, c + w, y, bright);
    // 大面积弱光（提供基础照明，避免纯黑；法线朝下）
    add_ceiling_light_quad(s, 0.4f, 1.55f, 1.2f, 1.95f, y, dim);

    // 中央方台（受高光直射，形成强对比）
    add_box(s, {0.85f, 0.0f, 0.85f}, {1.15f, 0.50f, 1.15f}, white);
    s.finalize();
    return s;
}

} // anonymous namespace

Scene build_scene(const std::string& name, int tri_budget, int light_count) {
    if (name == "cornell")   return build_cornell(tri_budget, light_count);
    if (name == "occlusion") return build_occlusion();
    if (name == "hdr")       return build_hdr();
    throw std::runtime_error("unknown scene: " + name);
}

Camera build_camera(const std::string& name, int width, int height) {
    if (name == "cornell")
        return Camera(vec3(0.5f, 0.5f, -1.35f), vec3(0.5f, 0.5f, 0.5f), vec3(0, 1, 0), 42.0f, width, height);
    if (name == "occlusion")
        // 取景：沿隔墙平面（x=1）看入，两房间并排可见、隔墙呈边缘朝向。
        // 亮房间（左）与暗房间（右）在屏幕空间直接相邻 —— biased 复用的漏光
        // 只能出现在这条接缝附近（见 docs/01-design.md §6 / §7.2 E4）。
        return Camera(vec3(1.0f, 0.52f, -1.55f), vec3(1.0f, 0.48f, 0.55f), vec3(0, 1, 0), 46.0f, width, height);
    if (name == "hdr")
        return Camera(vec3(1.0f, 1.0f, -2.10f), vec3(1.0f, 0.95f, 1.0f), vec3(0, 1, 0), 44.0f, width, height);
    throw std::runtime_error("unknown scene for camera: " + name);
}

} // namespace restir
