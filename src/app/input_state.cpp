#include <app/input_state.hpp>

#include <mutex>

namespace app {

namespace {
bool inRange(int index, usize count) {
    return index >= 0 && static_cast<usize>(index) < count;
}
} // namespace

void InputState::handleKey(int key, bool down) {
    if (!inRange(key, kKeyCount)) {
        return;
    }
    std::unique_lock lock(m_mutex);
    m_key_down[key] = down;
    (down ? m_key_pressed_accum : m_key_released_accum)[key] = true;
}

void InputState::handleMouseButton(int button, bool down) {
    if (!inRange(button, kMouseButtonCount)) {
        return;
    }
    std::unique_lock lock(m_mutex);
    m_mouse_down[button] = down;
    (down ? m_mouse_pressed_accum : m_mouse_released_accum)[button] = true;
}

void InputState::handleMouseMove(f32 x, f32 y, f32 dx, f32 dy) {
    std::unique_lock lock(m_mutex);
    m_mouse_position = Eigen::Vector2f(x, y);
    m_mouse_delta_accum += Eigen::Vector2f(dx, dy);
}

void InputState::handleScroll(f32 x_offset, f32 y_offset) {
    std::unique_lock lock(m_mutex);
    m_scroll_accum += Eigen::Vector2f(x_offset, y_offset);
}

void InputState::beginFrame() {
    std::unique_lock lock(m_mutex);

    m_key_pressed_snapshot = m_key_pressed_accum;
    m_key_released_snapshot = m_key_released_accum;
    m_key_pressed_accum.fill(false);
    m_key_released_accum.fill(false);

    m_mouse_pressed_snapshot = m_mouse_pressed_accum;
    m_mouse_released_snapshot = m_mouse_released_accum;
    m_mouse_pressed_accum.fill(false);
    m_mouse_released_accum.fill(false);

    m_mouse_delta_snapshot = m_mouse_delta_accum;
    m_mouse_delta_accum = Eigen::Vector2f(0.0f, 0.0f);

    m_scroll_snapshot = m_scroll_accum;
    m_scroll_accum = Eigen::Vector2f(0.0f, 0.0f);
}

bool InputState::isKeyDown(int key) const {
    if (!inRange(key, kKeyCount)) {
        return false;
    }
    std::shared_lock lock(m_mutex);
    return m_key_down[key];
}

bool InputState::isKeyPressed(int key) const {
    if (!inRange(key, kKeyCount)) {
        return false;
    }
    std::shared_lock lock(m_mutex);
    return m_key_pressed_snapshot[key];
}

bool InputState::isKeyReleased(int key) const {
    if (!inRange(key, kKeyCount)) {
        return false;
    }
    std::shared_lock lock(m_mutex);
    return m_key_released_snapshot[key];
}

bool InputState::isMouseButtonDown(int button) const {
    if (!inRange(button, kMouseButtonCount)) {
        return false;
    }
    std::shared_lock lock(m_mutex);
    return m_mouse_down[button];
}

bool InputState::isMouseButtonPressed(int button) const {
    if (!inRange(button, kMouseButtonCount)) {
        return false;
    }
    std::shared_lock lock(m_mutex);
    return m_mouse_pressed_snapshot[button];
}

bool InputState::isMouseButtonReleased(int button) const {
    if (!inRange(button, kMouseButtonCount)) {
        return false;
    }
    std::shared_lock lock(m_mutex);
    return m_mouse_released_snapshot[button];
}

Eigen::Vector2f InputState::mousePosition() const {
    std::shared_lock lock(m_mutex);
    return m_mouse_position;
}

Eigen::Vector2f InputState::mouseDelta() const {
    std::shared_lock lock(m_mutex);
    return m_mouse_delta_snapshot;
}

Eigen::Vector2f InputState::scrollDelta() const {
    std::shared_lock lock(m_mutex);
    return m_scroll_snapshot;
}

} // namespace app
