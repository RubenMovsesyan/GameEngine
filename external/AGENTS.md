# common/

Code and dependencies shared by more than one engine module. A math library used by
both `graphics/` and a future `physics/` belongs here; anything only `graphics/` needs
does not.

## Rules

- **One external dependency per directory.** `common/glm/`, `common/stb/`, etc. Never
  drop loose headers into `common/` directly, and never mix two dependencies in one
  directory.
- Prefer a git submodule for a dependency that has an upstream repo, so it can be
  updated in place.
- Adding a directory here means adding its include path to `build.c` for every module
  that consumes it.
- Our own shared code follows the module layout: `include/` for the public API,
  `src/` for the implementation.

## Contents

- `devtools/` — submodule, header-only C utilities. `common/` is on every module's
  include path, so include as `<devtools/rlog.h>`.
  - `attr.h` — `__DEV_ONLY__` / `__NOT_YET_IMPLEMENTED__` attribute macros.
  - `rlog.h` — logging. Define `RLOG_IMPLEMENTATION` in exactly one translation unit.
  - `common.h` — fixed-width typedefs (`u32`, `f32`, …), `__MIN__` / `__MAX__` /
    `__STRINGIFY__`. Its CUDA block is behind `__CUDACC__`, so it costs us nothing.
