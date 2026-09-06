#pragma once

#include <gpu/handle.hpp>

#include <Eigen/Dense>

#include <concepts>
#include <span>
#include <vector>

namespace graphics::primitives::scene {

struct Vertex {
    Eigen::Vector3f position;
    Eigen::Vector3f normal;
    Eigen::Vector2f uv;
};

// Inverse-transpose of mat3(model), padded to mat4; correct under non-uniform scale.
inline Eigen::Matrix4f normalMatrix(const Eigen::Matrix4f& model) {
    Eigen::Matrix4f n = Eigen::Matrix4f::Identity();
    n.block<3, 3>(0, 0) = model.block<3, 3>(0, 0).inverse().transpose();
    return n;
}

// Per-instance vertex data: world transform + its precomputed normal matrix.
struct InstanceData {
    Eigen::Matrix4f transform;
    Eigen::Matrix4f normal_matrix; // derived from transform in InstancedMesh::uploadIfDirty
};

// Anything drawable: a vertex buffer, an index buffer, and how many instances to draw.
template <typename T>
concept Mesh = requires(const T& value) {
    { value.vertexBuffer() } -> std::same_as<sg_buffer>;
    { value.indexBuffer() } -> std::same_as<sg_buffer>;
    { value.indexCount() } -> std::same_as<u32>;
    { value.instanceCount() } -> std::same_as<u32>;
};

class InstancedMesh;

// A single non-instanced mesh: a vertex buffer and an index buffer, both GPU-resident.
class StandardMesh {
    public:
        static StandardMesh create(std::span<const Vertex> vertices, std::span<const u32> indices);
        // Shares the source's vertex/index buffers -- lossy, drops its per-instance transforms.
        static StandardMesh fromInstanced(const InstancedMesh&);

        sg_buffer vertexBuffer() const { return m_vertices ? *m_vertices : sg_buffer{}; }
        sg_buffer indexBuffer() const { return m_indices ? *m_indices : sg_buffer{}; }
        u32 indexCount() const { return m_index_count; }
        u32 instanceCount() const { return 1; }

    private:
        friend class InstancedMesh;

        StandardMesh() = default;

        gpu::BufferHandle m_vertices;
        gpu::BufferHandle m_indices;
        u32 m_index_count = 0;
};

static_assert(Mesh<StandardMesh>);

// A mesh drawn many times with per-instance transforms, uploaded to instanceBuffer().
class InstancedMesh {
    public:
        static InstancedMesh create(std::span<const Vertex> vertices, std::span<const u32> indices);
        // Shares the source's vertex/index buffers, starts with zero instances.
        static InstancedMesh fromStandard(const StandardMesh&);

        sg_buffer vertexBuffer() const { return m_vertices ? *m_vertices : sg_buffer{}; }
        sg_buffer indexBuffer() const { return m_indices ? *m_indices : sg_buffer{}; }
        u32 indexCount() const { return m_index_count; }
        u32 instanceCount() const { return static_cast<u32>(m_instances.size()); }

        u32 addInstance(const Eigen::Matrix4f& transform);
        void removeInstance(u32 index);
        // Marks dirty unconditionally -- can't know if the caller actually wrote through the pointer.
        Eigen::Matrix4f* mutableInstance(u32 index);

        sg_buffer instanceBuffer() const { return m_instance_buffer ? *m_instance_buffer : sg_buffer{}; }
        void uploadIfDirty();

    private:
        friend class StandardMesh;

        InstancedMesh() = default;

        gpu::BufferHandle m_vertices;
        gpu::BufferHandle m_indices;
        u32 m_index_count = 0;

        std::vector<InstanceData> m_instances;
        gpu::BufferHandle m_instance_buffer;
        bool m_dirty = true;
};

static_assert(Mesh<InstancedMesh>);

} // namespace graphics::primitives::scene
