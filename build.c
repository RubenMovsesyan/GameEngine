#include "buildcpp/build.h"

const char* BUILD_DIR = ".build";
const char* CXX = "clang++";
const char* CXX_STD = "-std=c++23";

// VMA resolves entry points through the pointers we hand it from the vk::raii
// dispatchers, not through the loader's exported symbols.
static void cxxStep(Build* build) {
    buildStepSetCompiler(build, (char*)CXX);
    buildAddCompilationFlag(build, (char*)CXX_STD);
    // Makes the vk:: structs aggregates so designated initializers work. Set here for
    // every step, since it changes class definitions and must never differ between them.
    buildAddCompilationFlag(build, "-DVULKAN_HPP_NO_CONSTRUCTORS");
    buildAddCompilationFlag(build, "-DVMA_STATIC_VULKAN_FUNCTIONS=0");
    buildAddCompilationFlag(build, "-DVMA_DYNAMIC_VULKAN_FUNCTIONS=1");
    buildAddInclude(build, newDirectInclude("gpu/include"));
    buildAddInclude(build, newDirectInclude("common"));
}

int main(int argc, char** argv) {
    initArena(1024 * 1024 * 16);

    // Plain newBuild so the build script itself is rebuilt with clang (C23), not clang++
    Build build_value = newBuild(BUILD_DIR, argc, argv);
    Build* build = &build_value;

    // gpu -> libgpu.a. Knows nothing about windows, GLFW or rendering.
    cxxStep(build);
    buildAddObject(build, newObject("gpu/src/vma_impl.cpp"));
    buildAddObject(build, newObject("gpu/src/instance.cpp"));
    buildAddObject(build, newObject("gpu/src/device.cpp"));
    buildAddObject(build, newObject("gpu/src/command.cpp"));
    buildAddObject(build, newObject("gpu/src/frame.cpp"));
    buildAddObject(build, newObject("gpu/src/swapchain.cpp"));
    buildAddObject(build, newObject("gpu/src/buffer.cpp"));
    buildStepSetOutput(build, "libgpu.a");

    // graphics -> libgraphics.a. The only module that touches GLFW.
    buildStep(build);
    cxxStep(build);
    buildAddInclude(build, newDirectInclude("graphics/include"));
    buildAddObject(build, newObject("graphics/src/window.cpp"));
    buildAddObject(build, newObject("graphics/src/renderer.cpp"));
    buildStepSetOutput(build, "libgraphics.a");

    // sandbox -> executable. Not named "sandbox": object files mirror source paths into
    // BUILD_DIR, so .build/sandbox is already a directory
    buildStep(build);
    cxxStep(build);
    buildAddInclude(build, newDirectInclude("graphics/include"));
    buildAddLinkingFlag(build, (char*)CXX_STD);
    buildAddObject(build, newObject("sandbox/src/main.cpp"));
    // ar archives carry no dependency info and ld resolves left to right, so
    // graphics must come before gpu.
    buildAddLink(build, newPathLinkWithDep("graphics", (char*)BUILD_DIR));
    buildAddLink(build, newPathLinkWithDep("gpu", (char*)BUILD_DIR));
    buildAddLink(build, newDirectLink("glfw"));
    buildAddLink(build, newDirectLink("vulkan"));
    buildStepSetOutput(build, "sandbox_app");

    buildBuild(build);

    return 0;
}
