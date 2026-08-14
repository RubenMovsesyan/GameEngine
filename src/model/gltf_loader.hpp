#pragma once

#include <gpu/device.hpp>
#include <model/model.hpp>

#include <filesystem>

namespace model {

Model loadGltf(const gpu::Device&, const std::filesystem::path&);

} // namespace model
