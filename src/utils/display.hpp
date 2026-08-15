#pragma once

#include <concepts>
#include <string>

namespace utils {

template <typename T>
concept Display = requires(const T value) {
    { value.display() } -> std::same_as<std::string>;
};

} // namespace utils
