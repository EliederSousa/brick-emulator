#include "brickemu/CoreRegistry.hpp"

#include "brickemu/SPL03.hpp"

namespace brickemu {

std::unique_ptr<ICpuCore> CoreRegistry::create(const BrickConfig& config, std::string* error) {
    if (config.core == "SPL03") {
        return std::make_unique<SPL03>(config);
    }

    if (error != nullptr) {
        *error = "unsupported core: " + config.core;
    }
    return nullptr;
}

} // namespace brickemu
