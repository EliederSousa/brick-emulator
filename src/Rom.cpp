#include "brickemu/Rom.hpp"

#include <fstream>

namespace brickemu {
namespace {

void setError(RomError* error, std::string message) {
    if (error != nullptr) {
        error->message = std::move(message);
    }
}

} // namespace

std::optional<Rom> Rom::load(const std::filesystem::path& path, RomError* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        setError(error, "ROM file not found, please add the required ROM to this path: " + path.string());
        return std::nullopt;
    }

    Rom rom;
    rom.data_ = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (rom.data_.empty()) {
        setError(error, "ROM file is empty: " + path.string());
        return std::nullopt;
    }

    return rom;
}

std::uint8_t Rom::byte(std::uint32_t address) const {
    return data_[address % data_.size()];
}

std::uint16_t Rom::word(std::uint32_t address) const {
    return static_cast<std::uint16_t>((byte(address) << 8U) | byte(address + 1));
}

std::uint16_t Rom::wordLsb(std::uint32_t address) const {
    return static_cast<std::uint16_t>(byte(address) | (byte(address + 1) << 8U));
}

std::uint64_t Rom::bytes(std::uint32_t address, std::uint32_t count) const {
    std::uint64_t result = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        result |= static_cast<std::uint64_t>(byte(address + i)) << (8U * (count - i - 1));
    }
    return result;
}

void Rom::writeByte(std::uint32_t address, std::uint8_t value) {
    if (address < data_.size()) {
        data_[address] = value;
    }
}

void Rom::writeWord(std::uint32_t address, std::uint16_t value) {
    if (address + 1 < data_.size()) {
        data_[address] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        data_[address + 1] = static_cast<std::uint8_t>(value & 0xFFU);
    }
}

std::uint32_t Rom::size() const {
    return static_cast<std::uint32_t>(data_.size());
}

std::uint32_t Rom::mask() const {
    std::uint32_t bits = 0;
    std::uint32_t value = size();
    while (value > 0) {
        ++bits;
        value >>= 1U;
    }
    return (1U << bits) - 1U;
}

std::span<const std::uint8_t> Rom::data() const {
    return data_;
}

} // namespace brickemu
