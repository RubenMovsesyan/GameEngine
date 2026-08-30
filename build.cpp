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

	engine.addInclude(Include<Direct>("external/NVRHI/include"));
	engine.addInclude(Include<Direct>("external/devtools"));
	engine.addInclude(Include<Direct>("external/Eigen"));
	engine.addInclude(Include<Direct>("external/cgltf"));
	engine.addInclude(Include<Direct>("external/stb"));
	engine.addInclude(Include<Direct>("src"));
	engine.addInclude(Include<Direct>(".build/shaders"));

	engine.addLink(Link<Dependency>("vulkan"));
	engine.addLink(Link<Dependency>("pthread"));
	engine.addLink(Link<Dependency>("glfw"));

	// Shaders -> SPIR-V embedded as C headers, so the binary carries its own bytecode.
	// Real Tasks (not raw exec()'d Commands) so C++ files that #include the generated
	// headers below get tracked as depending on them and rebuild when a shader changes.
	const std::vector<std::string> shader_names = {
	    "gbuffer.vert", "gbuffer_instanced.vert", "gbuffer.frag", "fullscreen_triangle.vert", "lighting.frag", "tonemap.frag",
	    "sky.frag", "fxaa.frag"};

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

	// NVRHI, built from source: common bookkeeping, validation layer, Vulkan backend.
	const std::vector<std::string> nvrhi_sources = {
	    "external/NVRHI/src/common/format-info.cpp",
	    "external/NVRHI/src/common/misc.cpp",
	    "external/NVRHI/src/common/state-tracking.cpp",
	    "external/NVRHI/src/common/utils.cpp",
	    "external/NVRHI/src/common/aftermath.cpp",

	    "external/NVRHI/src/validation/validation-commandlist.cpp",
	    "external/NVRHI/src/validation/validation-device.cpp",

	    "external/NVRHI/src/vulkan/vulkan-allocator.cpp",
	    "external/NVRHI/src/vulkan/vulkan-buffer.cpp",
	    "external/NVRHI/src/vulkan/vulkan-commandlist.cpp",
	    "external/NVRHI/src/vulkan/vulkan-compute.cpp",
	    "external/NVRHI/src/vulkan/vulkan-constants.cpp",
	    "external/NVRHI/src/vulkan/vulkan-device.cpp",
	    "external/NVRHI/src/vulkan/vulkan-graphics.cpp",
	    "external/NVRHI/src/vulkan/vulkan-meshlets.cpp",
	    "external/NVRHI/src/vulkan/vulkan-queries.cpp",
	    "external/NVRHI/src/vulkan/vulkan-queue.cpp",
	    "external/NVRHI/src/vulkan/vulkan-raytracing.cpp",
	    "external/NVRHI/src/vulkan/vulkan-resource-bindings.cpp",
	    "external/NVRHI/src/vulkan/vulkan-shader.cpp",
	    "external/NVRHI/src/vulkan/vulkan-staging-texture.cpp",
	    "external/NVRHI/src/vulkan/vulkan-state-tracking.cpp",
	    "external/NVRHI/src/vulkan/vulkan-texture.cpp",
	    "external/NVRHI/src/vulkan/vulkan-upload.cpp",
	};

	const std::vector<std::string> engine_sources = {
	    "src/gpu/instance.cpp",
	    "src/gpu/device.cpp",
	    "src/gpu/swapchain.cpp",

	    "src/graphics/surface.cpp",
	    "src/graphics/renderer.cpp",
	    "src/graphics/camera.cpp",
	    "src/graphics/mesh.cpp",
	    "src/graphics/texture.cpp",
	    "src/graphics/material.cpp",
	    "src/graphics/light.cpp",
	    "src/graphics/light_manager.cpp",
	    "src/graphics/post_process_chain.cpp",

	    "src/scene/model.cpp",
	    "src/scene/scene.cpp",
	    "src/scene/gltf_loader.cpp",

	    "src/app/window.cpp",
	    "src/app/application.cpp",
	    "src/app/cli_parser.cpp",
	    "src/app/input_state.cpp",

	    "sandbox/model_test/main.cpp",
	};

	std::vector<std::reference_wrapper<Task>> objects;
	for (const auto& source : nvrhi_sources) {
	    objects.push_back(engine.addTask(Output(Object(source))));
	}
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
