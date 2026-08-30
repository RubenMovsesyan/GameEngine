#pragma once

#include <gpu/device.hpp>

#include <Eigen/Dense>

#include <concepts>
#include <span>
#include <vector>

namespace graphics {

struct Vertex {
    Eigen::Vector3f position;
    Eigen::Vector3f normal;
    Eigen::Vector2f uv;
};

// Correct normal transform for an arbitrary model matrix: transpose of the inverse
// of the linear block. mat3(model) is only right for rotation + uniform scale;
// non-uniform scale skews normals off the surface. Returned padded to mat4 so it
// drops into a push constant / vertex attribute without column-padding surprises.
inline Eigen::Matrix4f normalMatrix(const Eigen::Matrix4f& model) {
    Eigen::Matrix4f n = Eigen::Matrix4f::Identity();
    n.block<3, 3>(0, 0) = model.block<3, 3>(0, 0).inverse().transpose();
    return n;
}

// One instance's per-draw data: its world transform plus the matching normal matrix,
// precomputed on upload so the shader never inverts per vertex.
struct InstanceData {
    Eigen::Matrix4f transform;
    Eigen::Matrix4f normal_matrix; // filled by InstancedMesh::uploadIfDirty from transform
};

// Anything drawable: a vertex buffer, an index buffer, and how many instances to draw.
template <typename T>
concept Mesh = requires(const T& value) {
    { value.vertexBuffer() } -> std::same_as<nvrhi::IBuffer*>;
    { value.indexBuffer() } -> std::same_as<nvrhi::IBuffer*>;
    { value.indexCount() } -> std::same_as<u32>;
    { value.instanceCount() } -> std::same_as<u32>;
};

class InstancedMesh;

// A single non-instanced mesh: a vertex buffer and an index buffer, both GPU-resident.
class StandardMesh {
    public:
        static StandardMesh create(const gpu::Device&, std::span<const Vertex> vertices, std::span<const u32> indices);
        // Shares the source's vertex/index buffers -- lossy, drops its per-instance transforms.
        static StandardMesh fromInstanced(const InstancedMesh&);

        nvrhi::IBuffer* vertexBuffer() const { return m_vertices; }
        nvrhi::IBuffer* indexBuffer() const { return m_indices; }
        u32 indexCount() const { return m_index_count; }
        u32 instanceCount() const { return 1; }

    private:
        StandardMesh() = default;

        nvrhi::BufferHandle m_vertices;
        nvrhi::BufferHandle m_indices;
        u32 m_index_count = 0;
};

static_assert(Mesh<StandardMesh>);

// A mesh drawn many times with per-instance transforms, uploaded to instanceBuffer().
class InstancedMesh {
    public:
        static InstancedMesh create(const gpu::Device&, std::span<const Vertex> vertices, std::span<const u32> indices);
        // Shares the source's vertex/index buffers, starts with zero instances.
        static InstancedMesh fromStandard(const StandardMesh&);

        nvrhi::IBuffer* vertexBuffer() const { return m_vertices; }
        nvrhi::IBuffer* indexBuffer() const { return m_indices; }
        u32 indexCount() const { return m_index_count; }
        u32 instanceCount() const { return static_cast<u32>(m_instances.size()); }

        u32 addInstance(const Eigen::Matrix4f& transform);
        void removeInstance(u32 index);
        // Marks dirty unconditionally -- can't know if the caller actually wrote through the pointer.
        Eigen::Matrix4f* mutableInstance(u32 index);

        nvrhi::IBuffer* instanceBuffer() const { return m_instance_buffer; }
        void uploadIfDirty(const gpu::Device&);

    private:
        InstancedMesh() = default;

        nvrhi::BufferHandle m_vertices;
        nvrhi::BufferHandle m_indices;
        u32 m_index_count = 0;

        std::vector<InstanceData> m_instances;
        nvrhi::BufferHandle m_instance_buffer;
        bool m_dirty = true;
};

static_assert(Mesh<InstancedMesh>);

} // namespace graphics
