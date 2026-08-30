#pragma once

#include <graphics/camera.hpp>
#include <graphics/light.hpp>
#include <graphics/renderer.hpp>
#include <graphics/skybox.hpp>
#include <scene/model.hpp>
#include <utils/display.hpp>

#include <string>
#include <utility>
#include <vector>

namespace scene {

// Models grouped with the Lights that illuminate them.
class Scene {
    public:
        void addModel(Model model);
        void addLight(graphics::Light light);

        void render(graphics::Renderer&, const Eigen::Matrix4f& world_transform, const graphics::Camera&);
        const std::vector<graphics::Light>& lights() const { return m_lights; }
        std::vector<Model>& mutableModels() { return m_models; }

        void setSkybox(graphics::Skybox skybox) { m_skybox = std::move(skybox); }
        const graphics::Skybox& skybox() const { return m_skybox; }

        std::string display() const;

    private:
        std::vector<Model> m_models;
        std::vector<graphics::Light> m_lights;
        graphics::Skybox m_skybox;
};

static_assert(utils::Display<Scene>);

} // namespace scene
