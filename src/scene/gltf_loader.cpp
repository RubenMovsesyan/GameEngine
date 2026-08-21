#include <scene/gltf_loader.hpp>

#include <rlog.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstdlib>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

namespace scene {

namespace {

usize base64DecodedSize(std::string_view encoded) {
    usize len = encoded.size();
    usize padding = (len >= 1 && encoded[len - 1] == '=') + (len >= 2 && encoded[len - 2] == '=');
    return (len / 4) * 3 - padding;
}

std::optional<graphics::Texture> loadTexture(const gpu::Device& device, const cgltf_texture* tex, const std::filesystem::path& gltf_path,
                                              nvrhi::Format format) {
    if (tex == nullptr || tex->image == nullptr) {
        return std::nullopt;
    }
    const cgltf_image& image = *tex->image;

    std::vector<u8> owned_bytes;
    const u8* bytes = nullptr;
    usize byte_count = 0;

    if (image.buffer_view != nullptr) {
        bytes = static_cast<const u8*>(image.buffer_view->buffer->data) + image.buffer_view->offset;
        byte_count = image.buffer_view->size;
    } else if (image.uri != nullptr) {
        std::string_view uri = image.uri;
        constexpr std::string_view kBase64Marker = "base64,";
        usize marker = uri.find(kBase64Marker);
        if (uri.rfind("data:", 0) == 0 && marker != std::string_view::npos) {
            std::string_view encoded = uri.substr(marker + kBase64Marker.size());
            usize decoded_size = base64DecodedSize(encoded);
            void* decoded = nullptr;
            cgltf_options base64_options{};
            if (cgltf_load_buffer_base64(&base64_options, decoded_size, encoded.data(), &decoded) != cgltf_result_success) {
                RLOG(LL_FATAL, "gltf: failed to decode base64 texture data");
            }
            owned_bytes.assign(static_cast<u8*>(decoded), static_cast<u8*>(decoded) + decoded_size);
            free(decoded);
        } else {
            std::string decoded_uri(uri);
            cgltf_decode_uri(decoded_uri.data());
            std::filesystem::path image_path = gltf_path.parent_path() / decoded_uri;
            std::ifstream file(image_path, std::ios::binary);
            if (!file) {
                RLOG(LL_FATAL, "gltf: failed to open texture file %s", image_path.c_str());
            }
            owned_bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
        bytes = owned_bytes.data();
        byte_count = owned_bytes.size();
    } else {
        RLOG(LL_FATAL, "gltf: texture image has neither buffer_view nor uri");
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes, static_cast<int>(byte_count), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        RLOG(LL_FATAL, "gltf: failed to decode texture image (%s)", stbi_failure_reason());
    }

    graphics::Texture texture = graphics::Texture::create(
        device, std::span(pixels, static_cast<usize>(width) * static_cast<usize>(height) * 4), static_cast<u32>(width), static_cast<u32>(height), format
    );
    stbi_image_free(pixels);
    RLOG(LL_DEBUG, "gltf: loaded texture %dx%d", width, height);
    return texture;
}

void appendPrimitive(const gpu::Device& device, const cgltf_primitive& primitive, const Eigen::Matrix4f& transform,
                      const std::filesystem::path& gltf_path, Model& model) {
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

    std::optional<graphics::Texture> base_color_texture;
    if (primitive.material != nullptr && primitive.material->has_pbr_metallic_roughness) {
        base_color_texture =
            loadTexture(device, primitive.material->pbr_metallic_roughness.base_color_texture.texture, gltf_path, nvrhi::Format::SRGBA8_UNORM);
    }

    model.addPart(graphics::StandardMesh::create(device, vertices, indices), transform, std::move(base_color_texture));
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

void walkNode(const gpu::Device& device, const cgltf_node& node, const Eigen::Matrix4f& parent_transform, const std::filesystem::path& gltf_path,
              Model& model, Scene& scene) {
    float local[16];
    cgltf_node_transform_local(&node, local);
    const Eigen::Matrix4f transform = parent_transform * Eigen::Map<const Eigen::Matrix4f>(local);

    if (node.mesh != nullptr) {
        for (cgltf_size i = 0; i < node.mesh->primitives_count; i++) {
            appendPrimitive(device, node.mesh->primitives[i], transform, gltf_path, model);
        }
    }
    if (node.light != nullptr) {
        appendLight(*node.light, transform, scene);
    }
    for (cgltf_size i = 0; i < node.children_count; i++) {
        walkNode(device, *node.children[i], transform, gltf_path, model, scene);
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
        walkNode(device, *data->scene->nodes[i], Eigen::Matrix4f::Identity(), path, model, scene);
    }

    cgltf_free(data);
    scene.addModel(std::move(model));
    return scene;
}

} // namespace scene
