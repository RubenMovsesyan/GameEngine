#pragma once

#include <common.h>

#include <sokol_gfx.h>
#include <sokol_gl.h>

#include <Eigen/Dense>

#include <array>

namespace graphics::primitives::pass {

// Immediate-mode debug lines (AABBs, frustums, axes), drawn as an unlit overlay: no
// depth test, so shapes are visible through solid geometry. A thin wrapper over
// sokol_gl (external/sokol/util/sokol_gl.h), which does the actual vertex batching.
//
// Off by default. Every recording method below checks isEnabled() first and returns
// immediately when disabled -- no sgl_* call happens, so a disabled DebugDraw costs
// nothing but that one branch. Callers never need their own enabled-guards.
class DebugDraw {
    public:
        DebugDraw();
        ~DebugDraw();

        DebugDraw(DebugDraw&&) = delete;
        DebugDraw& operator=(DebugDraw&&) = delete;
        DebugDraw(const DebugDraw&) = delete;
        DebugDraw& operator=(const DebugDraw&) = delete;

        void setEnabled(bool enabled) { m_enabled = enabled; }
        bool isEnabled() const { return m_enabled; }

        void line(const Eigen::Vector3f& a, const Eigen::Vector3f& b, const Eigen::Vector4f& color);
        void aabb(const Eigen::AlignedBox3f&, const Eigen::Vector4f& color);
        // 8 corners in graphics::Frustum::corners() order: near {BL,BR,TL,TR} then far.
        void frustum(const std::array<Eigen::Vector3f, 8>& corners, const Eigen::Vector4f& color);
        void axes(const Eigen::Matrix4f& transform, f32 scale = 1.0f);

        // Call once per frame, before any of the methods above. No-op while disabled.
        void begin(const Eigen::Matrix4f& view_projection);
        // Call once per frame, inside an active sg pass. No-op while disabled, so
        // Renderer can call this unconditionally without its own enabled-check.
        void render();

    private:
        sgl_context m_context = {};
        bool m_enabled = false;
};

} // namespace graphics::primitives::pass
