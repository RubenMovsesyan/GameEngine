#include <assets/asset_library.hpp>

#include <cxxabi.h>

#include <cstdlib>

namespace assets {

std::string AssetLibrary::canonical(const std::filesystem::path& path) {
    return std::filesystem::weakly_canonical(std::filesystem::absolute(path)).string();
}

std::string AssetLibrary::demangle(const char* mangled) {
    int status = 0;
    char* out = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    std::string name = (status == 0 && out != nullptr) ? out : mangled;
    std::free(out);
    return name;
}

} // namespace assets
