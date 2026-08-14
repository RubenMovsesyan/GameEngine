#include <model/gltf_loader.hpp>

#include <rlog.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <vector>

namespace model {

namespace {

void appendPrimitive(const gpu::Device& device, const cgltf_primitive& primitive, const Eigen::Matrix4f& transform, Model& model) {
    if (primitive.type != cgltf_primitive_type_triangles) {
        RLOG(LL_FATAL, "gltf: only triangle primitives are supported");
    }
    if (primitive.indices == nullptr) {
        RLOG(LL_FATAL, "gltf: only indexed primitives are supported");
    }

    const cgltf_accessor* position_accessor = nullptr;
    const cgltf_accessor* normal_accessor = nullptr;
    const cgltf_accessor* uv_accessor = nullptr;
    for (cgltf_size i = 0; i < primitive.attributes_count; i++) {
        const cgltf_attribute& attr = primitive.attributes[i];
        switch (attr.type) {
            case cgltf_attribute_type_position:
                position_accessor = attr.data;
                break;
            case cgltf_attribute_type_normal:
                normal_accessor = attr.data;
                break;
            case cgltf_attribute_type_texcoord:
                if (attr.index == 0) {
                    uv_accessor = attr.data;
                }
                break;
            default:
                break;
        }
    }
    if (position_accessor == nullptr || normal_accessor == nullptr || uv_accessor == nullptr) {
        RLOG(LL_FATAL, "gltf: primitive missing POSITION/NORMAL/TEXCOORD_0");
    }

    std::vector<graphics::Vertex> vertices(position_accessor->count);
    for (cgltf_size i = 0; i < position_accessor->count; i++) {
        cgltf_accessor_read_float(position_accessor, i, vertices[i].position.data(), 3);
        cgltf_accessor_read_float(normal_accessor, i, vertices[i].normal.data(), 3);
        cgltf_accessor_read_float(uv_accessor, i, vertices[i].uv.data(), 2);
    }

    std::vector<u32> indices(primitive.indices->count);
    for (cgltf_size i = 0; i < primitive.indices->count; i++) {
        indices[i] = static_cast<u32>(cgltf_accessor_read_index(primitive.indices, i));
    }

    model.addPart(graphics::Mesh::create(device, vertices, indices), transform);
}

void walkNode(const gpu::Device& device, const cgltf_node& node, const Eigen::Matrix4f& parent_transform, Model& model) {
    float local[16];
    cgltf_node_transform_local(&node, local);
    const Eigen::Matrix4f transform = parent_transform * Eigen::Map<const Eigen::Matrix4f>(local);

    if (node.mesh != nullptr) {
        for (cgltf_size i = 0; i < node.mesh->primitives_count; i++) {
            appendPrimitive(device, node.mesh->primitives[i], transform, model);
        }
    }
    for (cgltf_size i = 0; i < node.children_count; i++) {
        walkNode(device, *node.children[i], transform, model);
    }
}

} // namespace

Model loadGltf(const gpu::Device& device, const std::filesystem::path& path) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
        RLOG(LL_FATAL, "gltf: failed to parse %s", path.c_str());
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        RLOG(LL_FATAL, "gltf: failed to load buffers for %s", path.c_str());
    }
    if (data->scene == nullptr) {
        cgltf_free(data);
        RLOG(LL_FATAL, "gltf: no default scene in %s", path.c_str());
    }

    Model model;
    for (cgltf_size i = 0; i < data->scene->nodes_count; i++) {
        walkNode(device, *data->scene->nodes[i], Eigen::Matrix4f::Identity(), model);
    }

    cgltf_free(data);
    return model;
}

} // namespace model
