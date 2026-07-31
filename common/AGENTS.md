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

Empty for now — nothing is shared by two modules yet.
