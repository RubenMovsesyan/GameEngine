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

	// Shaders -> SPIR-V embedded as C headers, so the binary carries its own bytecode.
	// Real Tasks (not raw exec()'d Commands) so C++ files that #include the generated
	// headers below get tracked as depending on them and rebuild when a shader changes.
	const std::vector<std::string> shader_names = {
	    "gbuffer.vert", "gbuffer_instanced.vert", "gbuffer.frag", "fullscreen_triangle.vert", "lighting.frag", "tonemap.frag",
	    "sky.frag", "fxaa.frag", "shadow_depth.vert", "shadow_depth_instanced.vert", "shadow_depth.frag", "shadow_debug.frag"};

	// A Command Output is always considered stale (see Output::isStale), so guard the
	// glslc invocation ourselves: only add the Task when the .h is missing or older than
	// its shader source. Skipped shaders just don't get a Task -- the depends_on() wiring
	// below tolerates a missing entry.
	auto shaderStale = [](const std::string& name) {
	    std::filesystem::path src = "src/graphics/shaders/" + name;
	    std::filesystem::path hdr = ".build/shaders/" + name + ".h";
	    return !std::filesystem::exists(hdr) || std::filesystem::last_write_time(src) > std::filesystem::last_write_time(hdr);
	};

	std::unordered_map<std::string, std::reference_wrapper<Task>> shader_tasks;
	for (const std::string& name : shader_names) {
	    if (!shaderStale(name)) continue;
	    Task& task = engine.addTask(Output(Command({"glslc", "-mfmt=c", "-o", ".build/shaders/" + name + ".h", "src/graphics/shaders/" + name})));
	    shader_tasks.emplace(name, task);
	}

	const std::vector<std::string> engine_sources = {
	    "src/gpu/sokol.cpp",
	    "src/gpu/handle.cpp",

	    "src/graphics/camera.cpp",
	    "src/graphics/frustum.cpp",
	    "src/graphics/shadow.cpp",
	    "src/graphics/light.cpp",
	    "src/graphics/mesh.cpp",
	    "src/graphics/texture.cpp",
	    "src/graphics/material.cpp",
	    "src/graphics/light_manager.cpp",


	    "src/app/application.cpp",
	    "src/app/cli_parser.cpp",
	    "src/app/input_state.cpp",

	    "sandbox/model_test/main.cpp",
	};

	std::vector<std::reference_wrapper<Task>> objects;
	for (const auto& source : engine_sources) {
	    Task& task = engine.addTask(Output(Object(source)));
	    if (source == "src/graphics/renderer.cpp") {
	        for (const std::string& name : shader_names) {
	            auto it = shader_tasks.find(name);
	            if (it != shader_tasks.end()) task.depends_on(it->second);
	        }
	    } else if (source == "src/graphics/post_process_chain.cpp") {
	        auto it = shader_tasks.find("fullscreen_triangle.vert");
	        if (it != shader_tasks.end()) task.depends_on(it->second);
	    }
	    objects.push_back(task);
	}

	Task& exe = engine.addTask(Output(Binary("sandbox/model_test")));
	for (Task& object : objects) {
	    exe.depends_on(object);
	}

	build.build();

	return 0;
}
