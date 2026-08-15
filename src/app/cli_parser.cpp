#include <app/cli_parser.hpp>

#include <rlog.h>

namespace app {

// Unknown flags and unparseable values are fatal.
void CliParser::parse() {
    for (int i = 1; i < argc_; ++i) {
        std::string token = argv_[i];
        if (!token.starts_with("--")) {
            continue;
        }

        std::string name = token.substr(2);
        auto        def  = arg_defs_.find(name);
        if (def == arg_defs_.end()) {
            RLOG(LL_FATAL, "Unknown argument: --%s", name.c_str());
        }

        if (std::holds_alternative<bool>(def->second)) {
            arg_values_[name] = true;
            continue;
        }

        if (i + 1 >= argc_) {
            RLOG(LL_FATAL, "Missing value for --%s", name.c_str());
        }
        std::string raw = argv_[++i];

        std::visit(
            [&](auto&& placeholder) {
                using T = std::decay_t<decltype(placeholder)>;

                if constexpr (std::same_as<T, std::string>) {
                    arg_values_[name] = raw;
                } else if constexpr (std::floating_point<T>) {
                    try {
                        arg_values_[name] = static_cast<T>(std::stod(raw));
                    } catch (...) {
                        RLOG(LL_FATAL, "Invalid number for --%s: %s", name.c_str(), raw.c_str());
                    }
                } else if constexpr (std::unsigned_integral<T>) {
                    try {
                        arg_values_[name] = static_cast<T>(std::stoull(raw));
                    } catch (...) {
                        RLOG(LL_FATAL, "Invalid number for --%s: %s", name.c_str(), raw.c_str());
                    }
                } else {
                    try {
                        arg_values_[name] = static_cast<T>(std::stoll(raw));
                    } catch (...) {
                        RLOG(LL_FATAL, "Invalid number for --%s: %s", name.c_str(), raw.c_str());
                    }
                }
            },
            def->second
        );
    }
}

} // namespace app
