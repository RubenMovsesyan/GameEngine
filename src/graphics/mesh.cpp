#include <graphics/mesh.hpp>

namespace graphics {

Mesh Mesh::create(const gpu::Device& device, std::span<const Vertex> vertices, std::span<const u32> indices) {
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

    Mesh mesh;
    mesh.m_vertices = device.createBuffer(vertex_desc, vertices.data(), vertices.size_bytes());
    mesh.m_indices = device.createBuffer(index_desc, indices.data(), indices.size_bytes());
    mesh.m_index_count = static_cast<u32>(indices.size());
    return mesh;
}

} // namespace graphics
