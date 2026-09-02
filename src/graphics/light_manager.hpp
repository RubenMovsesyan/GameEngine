#pragma once

#include <gpu/device.hpp>
#include <graphics/light.hpp>

#include <vector>

namespace graphics {

// Non-owning: caller keeps each Light alive while it's registered. Call build() after add()ing.
class LightManager {
    public:
        explicit LightManager(const gpu::Device&);

        void add(const Light&);
        void build();
        nvrhi::IBuffer* buffer() const { return m_buffer; }
        u32 count() const { return static_cast<u32>(m_lights.size()); }
        // First registered shadow-casting directional light, or nullptr. Used to aim the shadow cascades.
        const Light* directionalLight() const;

    private:
        const gpu::Device* m_device;
        std::vector<const Light*> m_lights;
        nvrhi::BufferHandle m_buffer;
};

} // namespace graphics
