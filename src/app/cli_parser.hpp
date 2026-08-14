#pragma once

#include <common.h>

#include <concepts>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

// Mirrors buildcpp's defineArg<T>/parseArgs/arg<T> (buildcpp/build.hpp) — same
// type-erased-variant dispatch by category, plus `bool` as a "nothing" category for
// presence-only flags (e.g. --verbose) that buildcpp's own version doesn't have.
namespace app {

template <typename T>
concept CliArgValue = std::same_as<T, std::string> || std::same_as<T, bool>
    || std::same_as<T, i8> || std::same_as<T, i16> || std::same_as<T, i32> || std::same_as<T, i64>
    || std::same_as<T, u8> || std::same_as<T, u16> || std::same_as<T, u32> || std::same_as<T, u64>
    || std::same_as<T, f32> || std::same_as<T, f64>;

using CliArgVariant = std::variant<std::string, bool, i8, i16, i32, i64, u8, u16, u32, u64, f32, f64>;

class CliParser {
public:
    CliParser(int argc, char** argv) : argc_(argc), argv_(argv) {}

    // T's active variant alternative is what parse() dispatches a --name's value
    // into. T = bool means --name takes no value; presence alone sets it true.
    template <CliArgValue T>
    void defineArg(const std::string& name) {
        arg_defs_[name] = T{};
    }

    void parse();

    template <CliArgValue T>
    std::optional<T> arg(const std::string& name) const {
        auto it = arg_values_.find(name);
        if (it == arg_values_.end()) {
            return std::nullopt;
        }

        if (const T* value = std::get_if<T>(&it->second)) {
            return *value;
        }
        return std::nullopt;
    }

private:
    int argc_;
    char** argv_;

    std::unordered_map<std::string, CliArgVariant> arg_defs_;
    std::unordered_map<std::string, CliArgVariant> arg_values_;
};

} // namespace app
