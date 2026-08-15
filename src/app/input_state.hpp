#pragma once

#include <common.h>

#include <Eigen/Dense>

#include <array>
#include <shared_mutex>

namespace app {

// Thread-safe: handle*()/beginFrame() take a unique lock, queries take a shared lock.
class InputState {
    public:
        void handleKey(int key, int action);
        void handleMouseButton(int button, int action);
        void handleCursorPos(f64 x, f64 y);
        void handleScroll(f64 x_offset, f64 y_offset);

        // Call once per frame from whichever worker drives the game loop.
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
        static constexpr usize kKeyCount = 349;        // GLFW_KEY_LAST + 1
        static constexpr usize kMouseButtonCount = 8;  // GLFW_MOUSE_BUTTON_LAST + 1

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
        Eigen::Vector2f m_mouse_position_last_frame{0.0f, 0.0f};
        Eigen::Vector2f m_mouse_delta_snapshot{0.0f, 0.0f};

        Eigen::Vector2f m_scroll_accum{0.0f, 0.0f};
        Eigen::Vector2f m_scroll_snapshot{0.0f, 0.0f};
};

} // namespace app
