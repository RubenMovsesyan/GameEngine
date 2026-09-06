#include <graphics/primitives/scene/mesh.hpp>

namespace graphics::primitives::scene {

namespace {

gpu::BufferHandle makeVertexBuffer(std::span<const Vertex> vertices, const char* label) {
    sg_buffer_desc desc = {};
    desc.size = vertices.size_bytes();
    desc.usage.vertex_buffer = true;
    desc.data = sg_range{vertices.data(), vertices.size_bytes()};
    desc.label = label;
    return gpu::makeBuffer(desc);
}

gpu::BufferHandle makeIndexBuffer(std::span<const u32> indices, const char* label) {
    sg_buffer_desc desc = {};
    desc.size = indices.size_bytes();
    desc.usage.index_buffer = true;
    desc.data = sg_range{indices.data(), indices.size_bytes()};
    desc.label = label;
    return gpu::makeBuffer(desc);
}

} // namespace

StandardMesh StandardMesh::create(std::span<const Vertex> vertices, std::span<const u32> indices) {
    StandardMesh mesh;
    mesh.m_vertices = makeVertexBuffer(vertices, "mesh vertex buffer");
    mesh.m_indices = makeIndexBuffer(indices, "mesh index buffer");
    mesh.m_index_count = static_cast<u32>(indices.size());
    return mesh;
}

StandardMesh StandardMesh::fromInstanced(const InstancedMesh& other) {
    StandardMesh mesh;
    mesh.m_vertices = other.m_vertices;
    mesh.m_indices = other.m_indices;
    mesh.m_index_count = other.indexCount();
    return mesh;
}

InstancedMesh InstancedMesh::create(std::span<const Vertex> vertices, std::span<const u32> indices) {
    InstancedMesh mesh;
    mesh.m_vertices = makeVertexBuffer(vertices, "instanced mesh vertex buffer");
    mesh.m_indices = makeIndexBuffer(indices, "instanced mesh index buffer");
    mesh.m_index_count = static_cast<u32>(indices.size());
    return mesh;
}

InstancedMesh InstancedMesh::fromStandard(const StandardMesh& other) {
    InstancedMesh mesh;
    mesh.m_vertices = other.m_vertices;
    mesh.m_indices = other.m_indices;
    mesh.m_index_count = other.indexCount();
    return mesh;
}

u32 InstancedMesh::addInstance(const Eigen::Matrix4f& transform) {
    // normal_matrix is (re)derived in uploadIfDirty -- leave it identity for now.
    m_instances.push_back({transform, Eigen::Matrix4f::Identity()});
    m_dirty = true;
    return static_cast<u32>(m_instances.size() - 1);
}

void InstancedMesh::removeInstance(u32 index) {
    m_instances.erase(m_instances.begin() + index);
    m_dirty = true;
}

Eigen::Matrix4f* InstancedMesh::mutableInstance(u32 index) {
    m_dirty = true;
    return &m_instances[index].transform;
}

void InstancedMesh::uploadIfDirty() {
    if (!m_dirty) {
        return;
    }

    if (m_instances.empty()) {
        m_instance_buffer = nullptr;
        m_dirty = false;
        return;
    }

    // Refresh every normal matrix -- a mutableInstance() caller may have rewritten any transform.
    for (InstanceData& instance : m_instances) {
        instance.normal_matrix = normalMatrix(instance.transform);
    }

    const usize byte_size = m_instances.size() * sizeof(InstanceData);

    sg_buffer_desc desc = {};
    desc.size = byte_size;
    desc.usage.vertex_buffer = true;
    desc.data = sg_range{m_instances.data(), byte_size};
    desc.label = "instanced mesh instance buffer";

    // Assigning through the handle destroys the previous buffer, if any.
    m_instance_buffer = gpu::makeBuffer(desc);
    m_dirty = false;
}

} // namespace graphics::primitives::scene
