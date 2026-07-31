#include "buildcpp/build.h"

const char* BUILD_DIR = ".build";
const char* CXX = "clang++";
const char* CXX_STD = "-std=c++23";

int main(int argc, char** argv) {
    initArena(1024 * 1024 * 16);

    // Plain newBuild so the build script itself is rebuilt with clang (C23), not clang++
    Build build_value = newBuild(BUILD_DIR, argc, argv);
    Build* build = &build_value;

    // graphics -> libgraphics.a
    buildStepSetCompiler(build, (char*)CXX);
    buildAddCompilationFlag(build, (char*)CXX_STD);
    buildAddInclude(build, newDirectInclude("graphics/include"));
    buildAddInclude(build, newDirectInclude("common"));
    buildAddObject(build, newObject("graphics/src/graphics.cpp"));
    buildStepSetOutput(build, "libgraphics.a");

    // sandbox -> executable. Not named "sandbox": object files mirror source paths into
    // BUILD_DIR, so .build/sandbox is already a directory
    buildStep(build);
    buildStepSetCompiler(build, (char*)CXX);
    buildAddCompilationFlag(build, (char*)CXX_STD);
    buildAddLinkingFlag(build, (char*)CXX_STD);
    buildAddInclude(build, newDirectInclude("graphics/include"));
    buildAddInclude(build, newDirectInclude("common"));
    buildAddObject(build, newObject("sandbox/src/main.cpp"));
    buildAddLink(build, newPathLinkWithDep("graphics", (char*)BUILD_DIR));
    buildAddLink(build, newDirectLink("glfw"));
    buildAddLink(build, newDirectLink("vulkan"));
    buildStepSetOutput(build, "sandbox_app");

    buildBuild(build);

    return 0;
}
