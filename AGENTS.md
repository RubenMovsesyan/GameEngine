# GameEngine

A game engine built as independent modules. Each module is a top-level directory that
compiles to its own static library; `sandbox/` is the executable that exercises them.

Currently: `gpu/` and `graphics/`.

## Layout

```
buildcpp/   git submodule, the build system (build.h)
build.c     the build script, C23
common/     shared across modules (see common/AGENTS.md)
gpu/        Vulkan device, queues, commands, sync, swapchain, buffers
graphics/   the graphics engine: window (GLFW) and renderer. links gpu
sandbox/    test executable
```

`gpu/` is a library of RAII value types, not a framework — no base class to subclass, no
singleton, no `shared_ptr` graph. `graphics/` drives it and calls into it; `gpu/` never
calls back. Two rules keep the split real, and a future `physics/` depends on both:

- **`gpu/` never includes GLFW, and never mentions `graphics/`.** The window layer passes
  it a `std::span<const char* const>` of instance extensions and a bare `vk::SurfaceKHR`.
- **No virtual functions in either module.** The extension point is that you record
  whatever commands you want onto a `vk::raii::CommandBuffer&`.

Check both after touching `gpu/`:

```sh
grep -rn 'GLFW\|glfw\|graphics/' gpu/     # only the comment in gpu/vk.hpp
nm -uC .build/libgpu.a | grep -i glfw     # empty
```

A module is laid out as:

```
<module>/include/<module>/   public API, the only headers other modules may include
<module>/src/                implementation and private headers
```

## Building

Needs `vulkan-headers`, `vulkan-validation-layers`, `vulkan-memory-allocator`, `glfw`.

```sh
clang -std=c23 build.c -o build   # once, to bootstrap
./build                           # every time after
```

`./build` rebuilds itself when `build.c` or `buildcpp/build.h` changes, and writes
`compile_commands.json`. Artifacts land in `.build/`.

Adding a source file means adding a `buildAddObject` line to `build.c` — there is no
globbing.

## Choices

- C++23, clang. The build script is C23 because `build.h` is a C header.
- Vulkan only. No render-backend abstraction until there is a second backend.
- GLFW for windowing and input.
- Linux only. No platform `#ifdef`s until there is a second platform.
- Shaders will be compiled by `build.c` with `glslc -mfmt=c` and embedded as headers,
  so the binary carries its own SPIR-V. Not wired up yet — `graphics/shaders/` is empty.

## Gotchas

- A build step's output must not share a name with a top-level source directory. Object
  files mirror source paths into `.build/`, so `sandbox/src/main.cpp` makes `.build/sandbox`
  a directory — hence the executable is `sandbox_app`.
- Use `newBuild`, not `newBuildWithCompiler`. The build script rebuilds itself with the
  default compiler, which has to be a C compiler; set `clang++` per step with
  `buildStepSetCompiler`.
- Compilation flags are per-step, not inherited. `cxxStep()` in `build.c` applies the
  shared ones so `-DVULKAN_HPP_NO_CONSTRUCTORS` can never differ between steps — it
  changes `vk::` class definitions, so a mismatch would be an ODR violation.
- Never call a method on a bare `vk::Device` / `vk::CommandBuffer` / `vk::Queue`. Those
  route through a second, static dispatcher. `vk::raii::` types carry their own. Bare
  handles are passed, never called on.
