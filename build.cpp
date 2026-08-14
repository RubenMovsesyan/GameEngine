#include "buildcpp/build.hpp"

const std::string BUILD_DIR = ".build";

int main(int argc, char** argv) {
    Build build(BUILD_DIR, "clang++", argc, argv);

    BuildGroup& engine = build.addGroup();
    engine.addCompileFlag("-std=c++23");
    engine.addLinkFlag("-std=c++23");

    engine.addInclude(Include<Direct>("external/NVRHI/include"));
    engine.addInclude(Include<Direct>("external"));

    engine.addLink(Link<Dependency>("vulkan"));
    engine.addLink(Link<Dependency>("pthread"));

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

    std::vector<std::reference_wrapper<Task>> objects;
    for (const auto& source : nvrhi_sources) {
        objects.push_back(engine.addTask(Output(Object(source))));
    }
    objects.push_back(engine.addTask(Output(Object("src/main.cpp"))));

    Task& exe = engine.addTask(Output(Binary("game")));
    for (Task& object : objects) {
        exe.depends_on(object);
    }

    build.build();

    return 0;
}
