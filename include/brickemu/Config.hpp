#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace brickemu {

struct MaskOptions {
    std::unordered_map<std::string, int> portPullup;
    std::unordered_map<std::string, int> portPkey;
    int nonCrystalDiv = 0;
};

struct BrickConfig {
    std::string core;
    std::filesystem::path configPath;
    std::filesystem::path romPath;
    std::uint32_t clockHz = 0;
    MaskOptions maskOptions;
};

struct ConfigError {
    std::string message;
};

class ConfigLoader {
public:
    static std::optional<BrickConfig> load(const std::filesystem::path& path, ConfigError* error = nullptr);

private:
    static std::filesystem::path resolveRelative(const std::filesystem::path& configPath, const std::string& value);
};

} // namespace brickemu
