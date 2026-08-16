#include <scene/gltf_loader.hpp>

#include <rlog.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <vector>

namespace scene {

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

    model.addPart(graphics::StandardMesh::create(device, vertices, indices), transform);
}

graphics::LightType toLightType(cgltf_light_type type) {
    switch (type) {
        case cgltf_light_type_directional:
            return graphics::LightType::Directional;
        case cgltf_light_type_point:
            return graphics::LightType::Point;
        case cgltf_light_type_spot:
            return graphics::LightType::Spot;
        default:
            RLOG(LL_FATAL, "gltf: unsupported light type");
    }
    __builtin_unreachable(); // RLOG(LL_FATAL, ...) exits the process
}

void appendLight(const cgltf_light& light, const Eigen::Matrix4f& transform, Scene& scene) {
    // Rotation applied to local -Z, skipping inverse-transpose (matches gbuffer.vert's normal handling).
    const Eigen::Vector3f direction = (transform.block<3, 3>(0, 0) * Eigen::Vector3f(0.0f, 0.0f, -1.0f)).normalized();

    scene.addLight(graphics::Light{
        .type = toLightType(light.type),
        .color = Eigen::Vector3f(light.color[0], light.color[1], light.color[2]),
        .intensity = light.intensity,
        .position = transform.block<3, 1>(0, 3),
        .direction = direction,
        .range = light.range,
        .spot_inner_cone_angle = light.spot_inner_cone_angle,
        .spot_outer_cone_angle = light.spot_outer_cone_angle,
    });
}

void walkNode(const gpu::Device& device, const cgltf_node& node, const Eigen::Matrix4f& parent_transform, Model& model, Scene& scene) {
    float local[16];
    cgltf_node_transform_local(&node, local);
    const Eigen::Matrix4f transform = parent_transform * Eigen::Map<const Eigen::Matrix4f>(local);

    if (node.mesh != nullptr) {
        for (cgltf_size i = 0; i < node.mesh->primitives_count; i++) {
            appendPrimitive(device, node.mesh->primitives[i], transform, model);
        }
    }
    if (node.light != nullptr) {
        appendLight(*node.light, transform, scene);
    }
    for (cgltf_size i = 0; i < node.children_count; i++) {
        walkNode(device, *node.children[i], transform, model, scene);
    }
}

} // namespace

Scene loadGltf(const gpu::Device& device, const std::filesystem::path& path) {
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

    Scene scene;
    Model model;
    for (cgltf_size i = 0; i < data->scene->nodes_count; i++) {
        walkNode(device, *data->scene->nodes[i], Eigen::Matrix4f::Identity(), model, scene);
    }

    cgltf_free(data);
    scene.addModel(std::move(model));
    return scene;
}

} // namespace scene
