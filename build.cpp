#include "buildcpp/build.hpp"

const std::string BUILD_DIR = ".build";

int main(int argc, char** argv) {
	initLog(4096);
	Build build(BUILD_DIR, "clang++", argc, argv);

	// mkdir has no staleness of its own to track -- idempotent, just needs to run before
	// any shader Task below.
	Command mkdir_shaders({"mkdir", "-p", ".build/shaders"});
	mkdir_shaders.exec();

	BuildGroup& engine = build.addGroup();
	engine.addCompileFlag("-std=c++23");
	engine.addLinkFlag("-std=c++23");

	engine.addInclude(Include<Direct>("external/sokol"));
	engine.addInclude(Include<Direct>("external/sokol/util"));
	engine.addInclude(Include<Direct>("external/imgui"));
	engine.addInclude(Include<Direct>("external/devtools"));
	engine.addInclude(Include<Direct>("external/Eigen"));
	engine.addInclude(Include<Direct>("external/cgltf"));
	engine.addInclude(Include<Direct>("external/stb"));
	engine.addInclude(Include<Direct>("src"));
	engine.addInclude(Include<Direct>(".build/shaders"));

	engine.addLink(Link<Dependency>("vulkan"));
	engine.addLink(Link<Dependency>("pthread"));

	// sokol_app on Linux: X11 windowing plus dlopen for the Vulkan loader.
	engine.addLink(Link<Dependency>("X11"));
	engine.addLink(Link<Dependency>("Xi"));
	engine.addLink(Link<Dependency>("Xcursor"));
	engine.addLink(Link<Dependency>("dl"));
	engine.addLink(Link<Dependency>("m"));

	// Shaders -> a single sokol-shdc generated header carrying the SPIR-V for every program,
	// so the binary keeps shipping its own bytecode. A real Task (not a raw exec()'d Command),
	// so every TU that includes the generated header (see includes_shader_header below) is
	// tracked as depending on it and rebuilds when a shader changes.
	const std::string shader_source = "src/graphics/shaders/engine.glsl";
	const std::string shader_header = ".build/shaders/engine.glsl.h";

	// A Command Output is always considered stale (see Output::isStale), so guard the
	// sokol-shdc invocation ourselves: only add the Task when the header is missing or older
	// than its source.
	const bool shader_stale = !std::filesystem::exists(shader_header) ||
	                          std::filesystem::last_write_time(shader_source) > std::filesystem::last_write_time(shader_header);

	Task* shader_task = nullptr;
	if (shader_stale) {
	    shader_task = &engine.addTask(Output(Command({"external/sokol-tools-bin/bin/linux/sokol-shdc", "-i", shader_source, "-o",
	                                                  shader_header, "-l", "spirv_vk", "-f", "sokol"})));
	}

	const std::vector<std::string> engine_sources = {
	    "src/gpu/sokol.cpp",
	    "src/gpu/handle.cpp",

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

	    "src/graphics/presenter.cpp",

	    "src/scene/model.cpp",
	    "src/scene/gltf_loader.cpp",


	    "src/app/application.cpp",
	    "src/app/cli_parser.cpp",
	    "src/app/input_state.cpp",
	    "src/app/debug_ui.cpp",
	};

	// One binary per sandbox: the shared engine objects + that sandbox's own entry point.
	// Each entry file is named after its directory (not a shared "main.cpp") so the build
	// system's stem-keyed dependency map keeps the two binaries' objects separate.
	const std::vector<std::string> sandbox_mains = {
	    "sandbox/model_test/model_test.cpp",
	    "sandbox/point_test/point_test.cpp",
	};

	std::vector<std::reference_wrapper<Task>> engine_objects;
	for (const auto& source : engine_sources) {
	    Task& task = engine.addTask(Output(Object(source)));
	    // Every TU that includes the generated engine.glsl.h must rebuild when a shader changes.
	    const bool includes_shader_header = source == "src/graphics/renderers/deferred_renderer.cpp" ||
	                                        source == "src/graphics/renderers/point_renderer.cpp" ||
	                                        source == "src/graphics/primitives/pass/post.cpp" ||
	                                        source == "src/graphics/presenter.cpp";
	    if (includes_shader_header && shader_task != nullptr) {
	        task.depends_on(*shader_task);
	    }
	    engine_objects.push_back(task);
	}

	for (const auto& main_source : sandbox_mains) {
	    Task& main_object = engine.addTask(Output(Object(main_source)));
	    const std::filesystem::path binary_name = std::filesystem::path(main_source).parent_path();
	    Task& exe = engine.addTask(Output(Binary(binary_name)));
	    for (Task& object : engine_objects) {
	        exe.depends_on(object);
	    }
	    exe.depends_on(main_object);
	}

	build.build();

	return 0;
}
