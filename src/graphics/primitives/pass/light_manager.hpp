#pragma once

#include <gpu/handle.hpp>
#include <graphics/primitives/scene/light.hpp>

#include <vector>

namespace graphics::primitives::pass {

// Non-owning: caller keeps each Light alive while it's registered. Call build() after add()ing.
class LightManager {
    public:
        LightManager() = default;

        void add(const scene::Light&);
        void build();

        // Storage-buffer view, bound straight into sg_bindings.views[]. Invalid until build().
        sg_view view() const { return m_view ? *m_view : sg_view{}; }
        u32 count() const { return static_cast<u32>(m_lights.size()); }
        // First registered shadow-casting directional light, or nullptr. Used to aim the shadow cascades.
        const scene::Light* directionalLight() const;

    private:
        std::vector<const scene::Light*> m_lights;
        gpu::BufferHandle m_buffer;
        gpu::ViewHandle m_view;
};

} // namespace graphics::primitives::pass
