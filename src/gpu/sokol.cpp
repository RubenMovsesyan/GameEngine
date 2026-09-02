// The one translation unit that compiles sokol's implementations. Every other file
// includes the same headers without SOKOL_IMPL and just gets the declarations.
//
// Backend is picked here and nowhere else -- SOKOL_VULKAN makes sokol_app create the
// VkInstance/VkDevice/VkQueue and swapchain that sokol_gfx then renders through
// (sokol_glue's sglue_environment()/sglue_swapchain() do the handoff).

#define SOKOL_IMPL
#define SOKOL_VULKAN

// Keep our own main(): the sandbox parses argv before it ever wants a window, so the app
// calls sapp_run() itself rather than letting sokol_app define main() and hand us sokol_main().
#define SOKOL_NO_ENTRY

// sokol_app.h must come first: it is the header that defines VK_USE_PLATFORM_XLIB_KHR
// before pulling in <vulkan/vulkan.h>. sokol_gfx.h includes vulkan.h without it, and the
// include guard means whoever gets there first decides whether vkCreateXlibSurfaceKHR exists.
#include <sokol_app.h>

#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>

#define SOKOL_GL_IMPL
#include <sokol_gl.h>

// sokol_imgui.h's C++ implementation calls straight into Dear ImGui's C++ API, so imgui.h
// must be visible before the impl block, same as sokol_gfx.h has to precede sokol_gl.h.
#include <imgui.h>

#define SOKOL_IMGUI_IMPL
#include <sokol_imgui.h>
