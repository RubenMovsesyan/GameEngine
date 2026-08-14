#include "buildcpp/build.hpp"

const std::string BUILD_DIR = ".build";

int main(int argc, char** argv) {
	initLog(4096);
	Build build(BUILD_DIR, "clang++", argc, argv);

	// Shaders -> SPIR-V embedded as C headers (glslc -mfmt=c), so the binary carries its
	// own bytecode. Runs every build: a handful of tiny files, not worth caching.
	Command mkdir_shaders({"mkdir", "-p", ".build/shaders"});
	mkdir_shaders.exec();
	for (const std::string& name : {"gbuffer.vert", "gbuffer.frag", "lighting.vert", "lighting.frag"}) {
	    Command compile_shader({"glslc", "-mfmt=c", "-o", ".build/shaders/" + name + ".h", "src/graphics/shaders/" + name});
	    compile_shader.exec();
	}

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

	// NVRHI, built from source (no CMake): common bookkeeping, the validation layer,
	// and the Vulkan backend. D3D11/D3D12 left out — Linux only, no DirectX.
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

	    "src/app/window.cpp",
	    "src/app/application.cpp",
	    "src/app/cli_parser.cpp",

	    "sandbox/cube_test/main.cpp",
	};

	std::vector<std::reference_wrapper<Task>> objects;
	for (const auto& source : nvrhi_sources) {
	    objects.push_back(engine.addTask(Output(Object(source))));
	}
	for (const auto& source : engine_sources) {
	    objects.push_back(engine.addTask(Output(Object(source))));
	}

	Task& exe = engine.addTask(Output(Binary("sandbox/cube_test")));
	for (Task& object : objects) {
	    exe.depends_on(object);
	}

	build.build();

	return 0;
}
