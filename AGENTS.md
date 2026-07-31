# GameEngine

A game engine built as independent modules. Each module is a top-level directory that
compiles to its own static library; `sandbox/` is the executable that exercises them.

Currently: `graphics/` (Vulkan) only.

## Layout

```
buildcpp/   git submodule, the build system (build.h)
build.c     the build script, C23
common/     shared across modules (see common/AGENTS.md)
graphics/   the graphics engine
sandbox/    test executable
```

A module is laid out as:

```
<module>/include/<module>/   public API, the only headers other modules may include
<module>/src/                implementation and private headers
```

## Building

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
