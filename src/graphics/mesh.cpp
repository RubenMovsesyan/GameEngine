#include <graphics/mesh.hpp>

namespace graphics {

StandardMesh StandardMesh::create(const gpu::Device& device, std::span<const Vertex> vertices, std::span<const u32> indices) {
    nvrhi::BufferDesc vertex_desc{};
    vertex_desc.byteSize = vertices.size_bytes();
    vertex_desc.isVertexBuffer = true;
    vertex_desc.initialState = nvrhi::ResourceStates::VertexBuffer;
    vertex_desc.keepInitialState = true;
    vertex_desc.debugName = "mesh vertex buffer";

    nvrhi::BufferDesc index_desc{};
    index_desc.byteSize = indices.size_bytes();
    index_desc.isIndexBuffer = true;
    index_desc.initialState = nvrhi::ResourceStates::IndexBuffer;
    index_desc.keepInitialState = true;
    index_desc.debugName = "mesh index buffer";

    StandardMesh mesh;
    mesh.m_vertices = device.createBuffer(vertex_desc, vertices.data(), vertices.size_bytes());
    mesh.m_indices = device.createBuffer(index_desc, indices.data(), indices.size_bytes());
    mesh.m_index_count = static_cast<u32>(indices.size());
    return mesh;
}

StandardMesh StandardMesh::fromInstanced(const InstancedMesh& other) {
    StandardMesh mesh;
    mesh.m_vertices = other.vertexBuffer();
    mesh.m_indices = other.indexBuffer();
    mesh.m_index_count = other.indexCount();
    return mesh;
}

InstancedMesh InstancedMesh::create(const gpu::Device& device, std::span<const Vertex> vertices, std::span<const u32> indices) {
    nvrhi::BufferDesc vertex_desc{};
    vertex_desc.byteSize = vertices.size_bytes();
    vertex_desc.isVertexBuffer = true;
    vertex_desc.initialState = nvrhi::ResourceStates::VertexBuffer;
    vertex_desc.keepInitialState = true;
    vertex_desc.debugName = "instanced mesh vertex buffer";

    nvrhi::BufferDesc index_desc{};
    index_desc.byteSize = indices.size_bytes();
    index_desc.isIndexBuffer = true;
    index_desc.initialState = nvrhi::ResourceStates::IndexBuffer;
    index_desc.keepInitialState = true;
    index_desc.debugName = "instanced mesh index buffer";

    InstancedMesh mesh;
    mesh.m_vertices = device.createBuffer(vertex_desc, vertices.data(), vertices.size_bytes());
    mesh.m_indices = device.createBuffer(index_desc, indices.data(), indices.size_bytes());
    mesh.m_index_count = static_cast<u32>(indices.size());
    return mesh;
}

InstancedMesh InstancedMesh::fromStandard(const StandardMesh& other) {
    InstancedMesh mesh;
    mesh.m_vertices = other.vertexBuffer();
    mesh.m_indices = other.indexBuffer();
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

void InstancedMesh::uploadIfDirty(const gpu::Device& device) {
    if (!m_dirty) {
        return;
    }

    // Refresh every normal matrix -- a mutableInstance() caller may have rewritten any transform.
    for (InstanceData& instance : m_instances) {
        instance.normal_matrix = normalMatrix(instance.transform);
    }

    nvrhi::BufferDesc instance_desc{};
    instance_desc.byteSize = m_instances.size() * sizeof(InstanceData);
    instance_desc.isVertexBuffer = true;
    instance_desc.initialState = nvrhi::ResourceStates::VertexBuffer;
    instance_desc.keepInitialState = true;
    instance_desc.debugName = "instanced mesh instance buffer";

    m_instance_buffer = device.createBuffer(instance_desc, m_instances.data(), instance_desc.byteSize);
    m_dirty = false;
}

} // namespace graphics
