#include <scene/gltf_loader.hpp>

#include <graphics/primitives/scene/material.hpp>

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

// Combined (embedded) or separate (external file / base64) -- always an owned copy.
std::vector<u8> resolveImageBytes(const cgltf_texture* tex, const std::filesystem::path& gltf_path) {
    if (tex == nullptr || tex->image == nullptr) {
        return {};
    }
    const cgltf_image& image = *tex->image;

    std::vector<u8> owned_bytes;

    if (image.buffer_view != nullptr) {
        const u8* view_bytes = static_cast<const u8*>(image.buffer_view->buffer->data) + image.buffer_view->offset;
        owned_bytes.assign(view_bytes, view_bytes + image.buffer_view->size);
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
    } else {
        RLOG(LL_FATAL, "gltf: texture image has neither buffer_view nor uri");
    }

    return owned_bytes;
}

// Runs on whatever thread calls it -- the background decode thread, for loadTexture().
graphics::primitives::scene::DecodedPixels decodeImageBytes(std::vector<u8> bytes) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        RLOG(LL_FATAL, "gltf: failed to decode texture image (%s)", stbi_failure_reason());
    }
    graphics::primitives::scene::DecodedPixels result{
        std::vector<u8>(pixels, pixels + static_cast<usize>(width) * static_cast<usize>(height) * 4), static_cast<u32>(width), static_cast<u32>(height)
    };
    stbi_image_free(pixels);
    RLOG(LL_DEBUG, "gltf: loaded texture %ux%u", result.width, result.height);
    return result;
}

std::optional<graphics::primitives::scene::Texture> loadTexture(const cgltf_texture* tex, const std::filesystem::path& gltf_path,
                                              sg_pixel_format format) {
    if (tex == nullptr || tex->image == nullptr) {
        return std::nullopt;
    }
    auto decode = [bytes = resolveImageBytes(tex, gltf_path)]() mutable { return decodeImageBytes(std::move(bytes)); };
    return graphics::primitives::scene::Texture::loadAsyncThread(std::move(decode), format);
}

graphics::primitives::scene::Material loadMaterial(const cgltf_material* material, const std::filesystem::path& gltf_path) {
    Eigen::Vector4f base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
    f32 metallic_factor = 1.0f;
    f32 roughness_factor = 1.0f;
    std::optional<graphics::primitives::scene::Texture> base_color_texture;
    std::optional<graphics::primitives::scene::Texture> metallic_roughness_texture;

    if (material != nullptr && material->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& pbr = material->pbr_metallic_roughness;
        base_color_factor = Eigen::Vector4f(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]);
        metallic_factor = pbr.metallic_factor;
        roughness_factor = pbr.roughness_factor;
        base_color_texture = loadTexture(pbr.base_color_texture.texture, gltf_path, SG_PIXELFORMAT_SRGB8A8);
        metallic_roughness_texture = loadTexture(pbr.metallic_roughness_texture.texture, gltf_path, SG_PIXELFORMAT_RGBA8);
    }

    return graphics::primitives::scene::Material::create(std::move(base_color_texture), std::move(metallic_roughness_texture), base_color_factor, metallic_factor,
                                       roughness_factor);
}

void appendPrimitive(const cgltf_primitive& primitive, const Eigen::Matrix4f& transform,
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

    std::vector<graphics::primitives::scene::Vertex> vertices(position_accessor->count);
    for (cgltf_size i = 0; i < position_accessor->count; i++) {
        cgltf_accessor_read_float(position_accessor, i, vertices[i].position.data(), 3);
        cgltf_accessor_read_float(normal_accessor, i, vertices[i].normal.data(), 3);
        cgltf_accessor_read_float(uv_accessor, i, vertices[i].uv.data(), 2);
    }

    // glTF POSITION accessors usually carry min/max; fall back to a scan when they don't.
    Eigen::AlignedBox3f local_bounds;
    if (position_accessor->has_min && position_accessor->has_max) {
        local_bounds.extend(Eigen::Vector3f(position_accessor->min[0], position_accessor->min[1], position_accessor->min[2]));
        local_bounds.extend(Eigen::Vector3f(position_accessor->max[0], position_accessor->max[1], position_accessor->max[2]));
    } else {
        for (const graphics::primitives::scene::Vertex& vertex : vertices) {
            local_bounds.extend(vertex.position);
        }
    }

    std::vector<u32> indices(primitive.indices->count);
    for (cgltf_size i = 0; i < primitive.indices->count; i++) {
        indices[i] = static_cast<u32>(cgltf_accessor_read_index(primitive.indices, i));
    }

    graphics::primitives::scene::Material material = loadMaterial(primitive.material, gltf_path);
    model.addPart(graphics::primitives::scene::StandardMesh::create(vertices, indices), transform, std::move(material), local_bounds);
}

graphics::primitives::scene::LightType toLightType(cgltf_light_type type) {
    switch (type) {
        case cgltf_light_type_directional:
            return graphics::primitives::scene::LightType::Directional;
        case cgltf_light_type_point:
            return graphics::primitives::scene::LightType::Point;
        case cgltf_light_type_spot:
            return graphics::primitives::scene::LightType::Spot;
        default:
            RLOG(LL_FATAL, "gltf: unsupported light type");
    }
    __builtin_unreachable(); // RLOG(LL_FATAL, ...) exits the process
}

void appendLight(const cgltf_light& light, const Eigen::Matrix4f& transform, std::vector<graphics::primitives::scene::Light>& lights) {
    // glTF lights aim down local -Z; inverse-transpose keeps a non-uniformly scaled node pointing true.
    const Eigen::Matrix3f normal_basis = transform.block<3, 3>(0, 0).inverse().transpose();
    const Eigen::Vector3f direction = (normal_basis * Eigen::Vector3f(0.0f, 0.0f, -1.0f)).normalized();

    const graphics::primitives::scene::LightType type = toLightType(light.type);
    lights.push_back(graphics::primitives::scene::Light{
        .type = type,
        .color = Eigen::Vector3f(light.color[0], light.color[1], light.color[2]),
        .intensity = light.intensity,
        .position = transform.block<3, 1>(0, 3),
        .direction = direction,
        .range = light.range,
        .spot_inner_cone_angle = light.spot_inner_cone_angle,
        .spot_outer_cone_angle = light.spot_outer_cone_angle,
        .casts_shadows = type != graphics::primitives::scene::LightType::Point, // glTF has no flag; point-light shadows aren't built
    });
}

void walkNode(const cgltf_node& node, const Eigen::Matrix4f& parent_transform, const std::filesystem::path& gltf_path,
              Model& model, std::vector<graphics::primitives::scene::Light>& lights) {
    float local[16];
    cgltf_node_transform_local(&node, local);
    const Eigen::Matrix4f transform = parent_transform * Eigen::Map<const Eigen::Matrix4f>(local);

    if (node.mesh != nullptr) {
        for (cgltf_size i = 0; i < node.mesh->primitives_count; i++) {
            appendPrimitive(node.mesh->primitives[i], transform, gltf_path, model);
        }
    }
    if (node.light != nullptr) {
        appendLight(*node.light, transform, lights);
    }
    for (cgltf_size i = 0; i < node.children_count; i++) {
        walkNode(*node.children[i], transform, gltf_path, model, lights);
    }
}

} // namespace

LoadedGltf loadGltf(const std::filesystem::path& path) {
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

    // One Model per root node that actually contributes a mesh -- a root node whose subtree
    // is lights-only still gets walked (for its lights) but its empty Model is discarded.
    LoadedGltf result;
    for (cgltf_size i = 0; i < data->scene->nodes_count; i++) {
        Model model;
        walkNode(*data->scene->nodes[i], Eigen::Matrix4f::Identity(), path, model, result.lights);
        if (model.partCount() > 0) {
            result.models.push_back(std::move(model));
        }
    }

    cgltf_free(data);
    return result;
}

} // namespace scene
