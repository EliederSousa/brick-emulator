#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace brickemu {

struct RomError {
    std::string message;
};

class Rom {
public:
    static std::optional<Rom> load(const std::filesystem::path& path, RomError* error = nullptr);

    std::uint8_t byte(std::uint32_t address) const;
    std::uint16_t word(std::uint32_t address) const;
    std::uint16_t wordLsb(std::uint32_t address) const;
    std::uint64_t bytes(std::uint32_t address, std::uint32_t count) const;

    void writeByte(std::uint32_t address, std::uint8_t value);
    void writeWord(std::uint32_t address, std::uint16_t value);

    std::uint32_t size() const;
    std::uint32_t mask() const;
    std::span<const std::uint8_t> data() const;

private:
    std::vector<std::uint8_t> data_;
};

} // namespace brickemu
