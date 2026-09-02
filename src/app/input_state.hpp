#pragma once

#include <common.h>

#include <Eigen/Dense>

#include <array>
#include <shared_mutex>

namespace app {

// Thread-safe: handle*()/beginFrame() take a unique lock, queries take a shared lock.
//
// Key and button indices are sokol_app's (SAPP_KEYCODE_*, SAPP_MOUSEBUTTON_*); this class
// never includes sokol_app.h itself -- Application translates sapp_event and calls in here.
class InputState {
    public:
        void handleKey(int key, bool down);
        void handleMouseButton(int button, bool down);
        // dx/dy are sokol_app's relative movement, which stays valid while the mouse is
        // locked -- absolute x/y does not, so the frame delta is accumulated, not derived.
        void handleMouseMove(f32 x, f32 y, f32 dx, f32 dy);
        void handleScroll(f32 x_offset, f32 y_offset);

        // Call once per frame, before reading any of the queries below.
        void beginFrame();

        bool isKeyDown(int key) const;
        bool isKeyPressed(int key) const;
        bool isKeyReleased(int key) const;
        bool isMouseButtonDown(int button) const;
        bool isMouseButtonPressed(int button) const;
        bool isMouseButtonReleased(int button) const;
        Eigen::Vector2f mousePosition() const;
        Eigen::Vector2f mouseDelta() const;
        Eigen::Vector2f scrollDelta() const;

    private:
        static constexpr usize kKeyCount = 512;       // SAPP_MAX_KEYCODES
        static constexpr usize kMouseButtonCount = 3; // SAPP_MOUSEBUTTON_{LEFT,RIGHT,MIDDLE}

        mutable std::shared_mutex m_mutex;

        std::array<bool, kKeyCount> m_key_down{};
        std::array<bool, kKeyCount> m_key_pressed_accum{};
        std::array<bool, kKeyCount> m_key_released_accum{};
        std::array<bool, kKeyCount> m_key_pressed_snapshot{};
        std::array<bool, kKeyCount> m_key_released_snapshot{};

        std::array<bool, kMouseButtonCount> m_mouse_down{};
        std::array<bool, kMouseButtonCount> m_mouse_pressed_accum{};
        std::array<bool, kMouseButtonCount> m_mouse_released_accum{};
        std::array<bool, kMouseButtonCount> m_mouse_pressed_snapshot{};
        std::array<bool, kMouseButtonCount> m_mouse_released_snapshot{};

        Eigen::Vector2f m_mouse_position{0.0f, 0.0f};
        Eigen::Vector2f m_mouse_delta_accum{0.0f, 0.0f};
        Eigen::Vector2f m_mouse_delta_snapshot{0.0f, 0.0f};

        Eigen::Vector2f m_scroll_accum{0.0f, 0.0f};
        Eigen::Vector2f m_scroll_snapshot{0.0f, 0.0f};
};

} // namespace app
