#pragma once

#include <gpu/device.hpp>

#include <Eigen/Dense>

#include <span>

namespace graphics {

struct Vertex {
    Eigen::Vector3f position;
    Eigen::Vector3f normal;
    Eigen::Vector2f uv;
};

// Everything drawable is a mesh: a vertex buffer and an index buffer, both GPU-resident.
class Mesh {
    public:
        static Mesh create(const gpu::Device&, std::span<const Vertex> vertices, std::span<const u32> indices);

        nvrhi::IBuffer* vertexBuffer() const { return m_vertices; }
        nvrhi::IBuffer* indexBuffer() const { return m_indices; }
        u32 indexCount() const { return m_index_count; }

    private:
        Mesh() = default;

        nvrhi::BufferHandle m_vertices;
        nvrhi::BufferHandle m_indices;
        u32 m_index_count = 0;
};

} // namespace graphics
