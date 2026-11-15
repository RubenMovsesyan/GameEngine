#include "buildcpp/build.hpp"
#include "buildcpp/cuda.hpp"
#include "build_recipe.hpp"

#include <cstdlib>
#include <map>
#include <utility>
#include <vector>

const std::string BUILD_DIR = ".build";

int main(int argc, char** argv) {
	initLog(4096);
	Build build(BUILD_DIR, "clang++", argc, argv);

    // Interop is optional; sandbox kernels still use nvcc.
    const auto toolkit = buildcpp::cuda::find();
    const bool cuda = toolkit.has_value();
    const std::string cuda_dir = toolkit ? toolkit->root.string() : "";
    auto engine_build = build.component<gameengine::GameEngine>({.root = ".", .options = {.cuda = toolkit}});
    BuildGroup& engine = engine_build.group;

	// Each CUDA sandbox's kernels: nvcc compiles them to PTX that the sandbox JIT-loads at
	// runtime with cuModuleLoad -- keeps nvcc out of the C++ link entirely. Output is
	// .build/cuda/<sandbox dir name>.ptx. Guarded like the shader task since a Command
	// Output is always considered stale.
	//   { .cu source, sandbox directory }
	const std::vector<std::pair<std::string, std::string>> cuda_kernels = {
	    {"sandbox/cuda_points_test/integrate.cu", "sandbox/cuda_points_test"},
	    {"sandbox/galaxy_sim/nbody.cu", "sandbox/galaxy_sim"},
	};
	std::map<std::string, Task*> cuda_ptx_task; // sandbox dir -> its nvcc task (absent if up to date)
	if (cuda) {
	    Command mkdir_cuda({"mkdir", "-p", ".build/cuda"});
	    mkdir_cuda.exec();
	    for (const auto& [cuda_src, sandbox_dir] : cuda_kernels) {
	        const std::string cuda_ptx =
	            ".build/cuda/" + std::filesystem::path(sandbox_dir).filename().string() + ".ptx";
	        const bool stale = !std::filesystem::exists(cuda_ptx) ||
	                           std::filesystem::last_write_time(cuda_src) > std::filesystem::last_write_time(cuda_ptx);
	        if (stale) {
	            cuda_ptx_task[sandbox_dir] = &engine.addTask(Output(Command({cuda_dir + "/bin/nvcc", "--ptx", "-arch=sm_89",
	                                                                         "-O3", "-Iexternal/devtools", cuda_src, "-o",
	                                                                         cuda_ptx})));
	        }
	    }
	}

	// One binary per sandbox: the shared engine objects + that sandbox's own entry point.
	// Each entry file is named after its directory (not a shared "main.cpp") so the build
	// system's stem-keyed dependency map keeps the two binaries' objects separate.
	std::vector<std::string> sandbox_mains = {
	    "sandbox/model_test/model_test.cpp",
	    "sandbox/point_test/point_test.cpp",
	    "sandbox/recorder_test/recorder_test.cpp",
	};
	if (cuda) {
	    sandbox_mains.push_back("sandbox/cuda_points_test/cuda_points_test.cpp");
	    sandbox_mains.push_back("sandbox/galaxy_sim/galaxy_sim.cpp");
	}

	for (const auto& main_source : sandbox_mains) {
	    Task& main_object = engine.addTask(Output(Object(main_source)));
	    const std::filesystem::path binary_name = std::filesystem::path(main_source).parent_path();
	    // If this sandbox has a .cu, order its main compile after nvcc has produced the PTX
	    // (the runtime cuModuleLoad path is baked into the source).
	    if (auto it = cuda_ptx_task.find(binary_name.string()); it != cuda_ptx_task.end() && it->second != nullptr) {
	        main_object.depends_on(*it->second);
	    }
	    Task& exe = engine.addTask(Output(Binary(std::filesystem::absolute(build.buildDir() / "bin" / binary_name))));
	    for (Task& object : engine_build.objects) {
	        exe.depends_on(object);
	    }
	    exe.depends_on(main_object);
	}

	build.build();

	return 0;
}
