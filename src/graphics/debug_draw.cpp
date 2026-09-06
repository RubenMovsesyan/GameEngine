#include <graphics/debug_draw.hpp>

namespace graphics {

DebugDraw::DebugDraw() {
    sgl_desc_t desc = {};
    sgl_setup(&desc);

    // The debug lines are drawn into DeferredRenderer's final LDR target -- an RGBA8 colour
    // attachment with no depth buffer -- not the swapchain. So the context's formats are
    // pinned to that target: RGBA8 colour, depth NONE (there is no depth attachment to
    // match), one sample. "No depth test" then comes from sokol_gl's own default
    // depth.compare = SG_COMPAREFUNC_ALWAYS. (An earlier version left these unset so
    // sg_make_pipeline would resolve them against the swapchain; that only worked while
    // this drew straight to the swapchain.)
    sgl_context_desc_t ctx_desc = {};
    ctx_desc.color_format = SG_PIXELFORMAT_RGBA8;
    ctx_desc.depth_format = SG_PIXELFORMAT_NONE;
    ctx_desc.sample_count = 1;
    m_context = sgl_make_context(&ctx_desc);
}

DebugDraw::~DebugDraw() {
    sgl_destroy_context(m_context);
    sgl_shutdown();
}

void DebugDraw::line(const Eigen::Vector3f& a, const Eigen::Vector3f& b, const Eigen::Vector4f& color) {
    if (!m_enabled) {
        return;
    }
    sgl_begin_lines();
    sgl_v3f_c4f(a.x(), a.y(), a.z(), color.x(), color.y(), color.z(), color.w());
    sgl_v3f_c4f(b.x(), b.y(), b.z(), color.x(), color.y(), color.z(), color.w());
    sgl_end();
}

void DebugDraw::aabb(const Eigen::AlignedBox3f& box, const Eigen::Vector4f& color) {
    if (!m_enabled) {
        return;
    }

    const Eigen::Vector3f& lo = box.min();
    const Eigen::Vector3f& hi = box.max();
    const std::array<Eigen::Vector3f, 8> corners{
        Eigen::Vector3f(lo.x(), lo.y(), lo.z()), Eigen::Vector3f(hi.x(), lo.y(), lo.z()),
        Eigen::Vector3f(hi.x(), hi.y(), lo.z()), Eigen::Vector3f(lo.x(), hi.y(), lo.z()),
        Eigen::Vector3f(lo.x(), lo.y(), hi.z()), Eigen::Vector3f(hi.x(), lo.y(), hi.z()),
        Eigen::Vector3f(hi.x(), hi.y(), hi.z()), Eigen::Vector3f(lo.x(), hi.y(), hi.z()),
    };
    // 4 bottom edges, 4 top edges, 4 verticals connecting them.
    constexpr std::array<std::pair<int, int>, 12> kEdges{
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
    };

    sgl_begin_lines();
    for (const auto& [i, j] : kEdges) {
        sgl_v3f_c4f(corners[i].x(), corners[i].y(), corners[i].z(), color.x(), color.y(), color.z(), color.w());
        sgl_v3f_c4f(corners[j].x(), corners[j].y(), corners[j].z(), color.x(), color.y(), color.z(), color.w());
    }
    sgl_end();
}

void DebugDraw::frustum(const std::array<Eigen::Vector3f, 8>& corners, const Eigen::Vector4f& color) {
    if (!m_enabled) {
        return;
    }

    // corners: near {BL,BR,TL,TR} = 0..3, far {BL,BR,TL,TR} = 4..7 (graphics::Frustum order).
    constexpr std::array<std::pair<int, int>, 12> kEdges{
        {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
    };

    sgl_begin_lines();
    for (const auto& [i, j] : kEdges) {
        sgl_v3f_c4f(corners[i].x(), corners[i].y(), corners[i].z(), color.x(), color.y(), color.z(), color.w());
        sgl_v3f_c4f(corners[j].x(), corners[j].y(), corners[j].z(), color.x(), color.y(), color.z(), color.w());
    }
    sgl_end();
}

void DebugDraw::axes(const Eigen::Matrix4f& transform, f32 scale) {
    if (!m_enabled) {
        return;
    }

    const Eigen::Vector3f origin = transform.block<3, 1>(0, 3);
    const Eigen::Vector3f x = origin + transform.block<3, 1>(0, 0).normalized() * scale;
    const Eigen::Vector3f y = origin + transform.block<3, 1>(0, 1).normalized() * scale;
    const Eigen::Vector3f z = origin + transform.block<3, 1>(0, 2).normalized() * scale;

    sgl_begin_lines();
    sgl_v3f_c4f(origin.x(), origin.y(), origin.z(), 1.0f, 0.0f, 0.0f, 1.0f);
    sgl_v3f_c4f(x.x(), x.y(), x.z(), 1.0f, 0.0f, 0.0f, 1.0f);
    sgl_v3f_c4f(origin.x(), origin.y(), origin.z(), 0.0f, 1.0f, 0.0f, 1.0f);
    sgl_v3f_c4f(y.x(), y.y(), y.z(), 0.0f, 1.0f, 0.0f, 1.0f);
    sgl_v3f_c4f(origin.x(), origin.y(), origin.z(), 0.0f, 0.0f, 1.0f, 1.0f);
    sgl_v3f_c4f(z.x(), z.y(), z.z(), 0.0f, 0.0f, 1.0f, 1.0f);
    sgl_end();
}

void DebugDraw::begin(const Eigen::Matrix4f& view_projection) {
    if (!m_enabled) {
        return;
    }

    sgl_set_context(m_context);
    sgl_defaults();
    sgl_matrix_mode_projection();
    sgl_load_matrix(view_projection.data()); // Eigen::Matrix4f is column-major by default
    sgl_matrix_mode_modelview();
    sgl_load_identity(); // every call above submits world-space coordinates already
}

void DebugDraw::render() {
    if (!m_enabled) {
        return;
    }
    sgl_context_draw(m_context);
}

} // namespace graphics
