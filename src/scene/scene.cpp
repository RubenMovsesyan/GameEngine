#include <scene/scene.hpp>

namespace {

std::string lightTypeName(graphics::LightType type) {
    switch (type) {
        case graphics::LightType::Directional:
            return "Directional";
        case graphics::LightType::Point:
            return "Point";
        case graphics::LightType::Spot:
            return "Spot";
    }
    return "Unknown";
}

} // namespace

namespace scene {

void Scene::addModel(Model model) {
    m_models.push_back(std::move(model));
}

void Scene::addLight(graphics::Light light) {
    m_lights.push_back(std::move(light));
}

void Scene::render(graphics::Renderer& renderer, const Eigen::Matrix4f& world_transform, const graphics::Camera& camera) {
    for (Model& model : m_models) {
        model.render(renderer, world_transform, camera);
    }
}

std::string Scene::display() const {
    std::string out = "Scene: " + std::to_string(m_models.size()) + " model(s), " + std::to_string(m_lights.size()) + " light(s)\n";

    for (usize i = 0; i < m_models.size(); i++) {
        out += "  Model[" + std::to_string(i) + "]: " + std::to_string(m_models[i].partCount()) + " mesh part(s)\n";
    }
    for (usize i = 0; i < m_lights.size(); i++) {
        out += "  Light[" + std::to_string(i) + "]: " + lightTypeName(m_lights[i].type) + "\n";
    }

    return out;
}

} // namespace scene
