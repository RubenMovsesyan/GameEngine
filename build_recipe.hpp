#pragma once

// Include the caller's buildcpp/cuda.hpp before this recipe. A recipe must not
// pull in its own nested copy of buildcpp.
#include <cstdlib>
#include <vector>

namespace gameengine {
struct GameEngine {
        static constexpr std::string_view id = "gameengine";
        struct Options {
                std::optional<buildcpp::cuda::Toolkit> cuda;
        };
        struct Exports {
                BuildGroup& group;
                std::vector<std::reference_wrapper<Task>> objects;
        };

        static Exports configure(Component& component, const Options& options) {
            std::filesystem::create_directories(component.buildDir() / "shaders");
            BuildGroup& engine = component.addGroup();
            engine.addCompileFlag("-std=c++23");
            engine.addLinkFlag("-std=c++23");

            engine.addInclude(Include<Direct>(component.source("external/sokol")));
            engine.addInclude(Include<Direct>(component.source("external/sokol/util")));
            engine.addInclude(Include<Direct>(component.source("external/imgui")));
            engine.addInclude(Include<Direct>(component.source("external/devtools")));
            engine.addInclude(Include<Direct>(component.source("external/Eigen")));
            engine.addInclude(Include<Direct>(component.source("external/cgltf")));
            engine.addInclude(Include<Direct>(component.source("external/stb")));
            engine.addInclude(Include<Direct>(component.source("src")));
            engine.addInclude(Include<Direct>(component.buildDir() / "shaders"));

            engine.addLink(Link<Dependency>("vulkan"));
            engine.addLink(Link<Dependency>("pthread"));

            // sokol_app on Linux: X11 windowing plus dlopen for the Vulkan loader.
            engine.addLink(Link<Dependency>("X11"));
            engine.addLink(Link<Dependency>("Xi"));
            engine.addLink(Link<Dependency>("Xcursor"));
            engine.addLink(Link<Dependency>("dl"));
            engine.addLink(Link<Dependency>("m"));

            if (options.cuda) {
                engine.addCompileFlag("-DGAMEENGINE_CUDA");
                options.cuda->configureDriver(engine);
            }
            const std::string shader_source = component.source("src/graphics/shaders/engine.glsl").string();
            const std::string shader_header = (component.buildDir() / "shaders/engine.glsl.h").string();

            // A Command Output is always considered stale (see Output::isStale), so guard the
            // sokol-shdc invocation ourselves: only add the Task when the header is missing or older
            // than its source.
            const bool shader_stale = !std::filesystem::exists(shader_header) ||
                                      std::filesystem::last_write_time(shader_source) > std::filesystem::last_write_time(shader_header);

            Task* shader_task = nullptr;
            if (shader_stale) {
                shader_task = &engine.addTask(Output(Command({component.source("external/sokol-tools-bin/bin/linux/sokol-shdc").string(), "-i", shader_source, "-o", shader_header, "-l", "spirv_vk", "-f", "sokol"})));
            }

            const std::vector<std::string> engine_sources = {
                "src/gpu/sokol.cpp",
                "src/gpu/handle.cpp",
                "src/gpu/external_buffer.cpp",
                "src/gpu/cross_semaphore.cpp",
                "src/gpu/cuda_interop.cpp", // fully #ifdef GAMEENGINE_CUDA -- compiles to nothing without it
                "src/gpu/shared_instance_buffer.cpp",
                "src/gpu/buffer_readback.cpp",

                "external/imgui/imgui.cpp",
                "external/imgui/imgui_draw.cpp",
                "external/imgui/imgui_tables.cpp",
                "external/imgui/imgui_widgets.cpp",

                "src/graphics/primitives/scene/camera.cpp",
                "src/graphics/primitives/scene/frustum.cpp",
                "src/graphics/primitives/scene/light.cpp",
                "src/graphics/primitives/scene/mesh.cpp",
                "src/graphics/primitives/scene/texture.cpp",
                "src/graphics/primitives/scene/material.cpp",

                "src/graphics/primitives/pass/shadow.cpp",
                "src/graphics/primitives/pass/light_manager.cpp",
                "src/graphics/primitives/pass/debug_draw.cpp",
                "src/graphics/primitives/pass/render_target.cpp",
                "src/graphics/primitives/pass/fullscreen_pass.cpp",
                "src/graphics/primitives/pass/post.cpp",

                "src/graphics/renderers/deferred_renderer.cpp",
                "src/graphics/renderers/point_renderer.cpp",
                "src/graphics/renderers/clear_renderer.cpp",

                "src/graphics/presenter.cpp",
                "src/graphics/recorder.cpp",

                "src/scene/model.cpp",

                "src/assets/asset_library.cpp",
                "src/assets/loaders/defaults.cpp",
                "src/assets/loaders/gltf.cpp",
                "src/assets/loaders/image.cpp",

                "src/app/context.cpp",
                "src/app/cli_parser.cpp",
                "src/app/input_state.cpp",
                "src/app/debug_ui.cpp",

                "src/utils/video_encoder.cpp",
            };

            std::vector<std::reference_wrapper<Task>> engine_objects;
            for (const auto& source : engine_sources) {
                Task& task = engine.addTask(Output(Object(component.source(source))));
                // Every TU that includes the generated engine.glsl.h must rebuild when a shader changes.
                const bool includes_shader_header = source == "src/graphics/renderers/deferred_renderer.cpp" ||
                                                    source == "src/graphics/renderers/point_renderer.cpp" ||
                                                    source == "src/graphics/primitives/pass/post.cpp" ||
                                                    source == "src/graphics/presenter.cpp" ||
                                                    source == "src/graphics/recorder.cpp";
                if (includes_shader_header && shader_task != nullptr) {
                    task.depends_on(*shader_task);
                }
                engine_objects.push_back(task);
            }

            return {engine, std::move(engine_objects)};
        }
};
static_assert(BuildRecipe<GameEngine>);
} // namespace gameengine
