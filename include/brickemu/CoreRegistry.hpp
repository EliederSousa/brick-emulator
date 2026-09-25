#pragma once

#include "brickemu/Config.hpp"
#include "brickemu/EmulatorRuntime.hpp"

#include <memory>
#include <string>

namespace brickemu {

class CoreRegistry {
public:
    static std::unique_ptr<ICpuCore> create(const BrickConfig& config, std::string* error = nullptr);
};

} // namespace brickemu
