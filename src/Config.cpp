#include "brickemu/Config.hpp"

#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>
#include <utility>

namespace brickemu {
namespace {

// Minimal INI reader for game configs: [section], key = value,
// '#' / ';' comments. Only this subset is supported.
using IniFile = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;

std::string trim(const std::string& s) {
    std::size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    std::size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

std::string unquote(std::string s) {
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

IniFile parseIni(std::istream& in) {
    IniFile ini;
    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto cut = std::min(line.find('#'), line.find(';'));
        if (cut != std::string::npos) {
            line.erase(cut);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        ini[section][trim(line.substr(0, eq))] = unquote(trim(line.substr(eq + 1)));
    }
    return ini;
}

const std::string* findKey(const IniFile& ini, const char* section, const char* key) {
    const auto s = ini.find(section);
    if (s == ini.end()) {
        return nullptr;
    }
    const auto k = s->second.find(key);
    if (k == s->second.end()) {
        return nullptr;
    }
    return &k->second;
}

bool parseU32(const std::string& text, std::uint32_t& out) {
    const auto* begin = text.data();
    return std::from_chars(begin, begin + text.size(), out).ec == std::errc{};
}

bool parseInt(const std::string& text, int& out) {
    const auto* begin = text.data();
    return std::from_chars(begin, begin + text.size(), out).ec == std::errc{};
}

void setError(ConfigError* error, std::string message) {
    if (error != nullptr) {
        error->message = std::move(message);
    }
}

} // namespace

std::optional<BrickConfig> ConfigLoader::load(const std::filesystem::path& path, ConfigError* error) {
    std::ifstream file(path);
    if (!file) {
        setError(error, "failed to open config: " + path.string());
        return std::nullopt;
    }

    const IniFile ini = parseIni(file);

    BrickConfig config;
    config.configPath = path;

    const auto* core = findKey(ini, "game", "core");
    const auto* clock = findKey(ini, "game", "clock");
    const auto* rom = findKey(ini, "game", "rom");
    if (core == nullptr || core->empty()) {
        setError(error, "config is missing: [game] core");
        return std::nullopt;
    }
    if (clock == nullptr || !parseU32(*clock, config.clockHz) || config.clockHz == 0) {
        setError(error, "config is missing or invalid: [game] clock");
        return std::nullopt;
    }
    if (rom == nullptr || rom->empty()) {
        setError(error, "config is missing: [game] rom");
        return std::nullopt;
    }
    config.core = *core;
    config.romPath = resolveRelative(path, *rom);

    if (const auto* div = findKey(ini, "game", "non_crystal_div")) {
        std::uint32_t value = 0;
        if (parseU32(*div, value)) {
            config.maskOptions.nonCrystalDiv = static_cast<int>(value);
        }
    }

    for (const auto* section : {"port_pullup", "port_pkey"}) {
        const auto s = ini.find(section);
        if (s == ini.end()) {
            continue;
        }
        auto& target = (std::string(section) == "port_pullup")
            ? config.maskOptions.portPullup
            : config.maskOptions.portPkey;
        for (const auto& [key, text] : s->second) {
            int value = 0;
            if (parseInt(text, value)) {
                target.emplace(key, value);
            }
        }
    }

    return config;
}

std::filesystem::path ConfigLoader::resolveRelative(const std::filesystem::path& configPath, const std::string& value) {
    const std::filesystem::path raw(value);
    if (raw.is_absolute()) {
        return raw;
    }

    const auto fromWorkingDirectory = std::filesystem::weakly_canonical(raw);
    if (std::filesystem::exists(fromWorkingDirectory)) {
        return fromWorkingDirectory;
    }

    return std::filesystem::weakly_canonical(configPath.parent_path() / raw);
}

} // namespace brickemu
