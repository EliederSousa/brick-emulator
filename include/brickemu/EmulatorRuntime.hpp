#pragma once

#include "brickemu/Config.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace brickemu {

struct CpuSnapshot {
    std::uint32_t pc = 0;
    std::uint8_t opcode = 0;
    std::uint8_t a = 0;
    std::uint8_t x = 0;
    std::uint8_t sp = 0;
    bool nf = false;
    bool vf = false;
    bool df = false;
    bool bf = false;
    bool interruptFlag = false;
    bool zf = false;
    bool cf = false;
};

class ICpuCore {
public:
    virtual ~ICpuCore() = default;
    virtual void reset() = 0;
    virtual std::uint32_t clock() = 0;
    virtual std::uint32_t pc() const = 0;
    virtual std::uint32_t romSize() const = 0;
    virtual std::uint32_t romMask() const = 0;
    virtual std::uint32_t instructionCounter() const = 0;
    virtual std::uint32_t cycleCounter() const = 0;
    virtual std::span<const std::uint8_t> vram() const = 0;
    virtual CpuSnapshot snapshot() const = 0;
    // Button input (Python parity with SPL0X.port_handler):
    // port "PA"/"PB"/"RES", level 0/1 = driven level, negative = released.
    virtual void setPortInput(const std::string& port, std::uint8_t mask, int level) = 0;
};

class IPeripheral {
public:
    virtual ~IPeripheral() = default;
    virtual void onClock(std::uint32_t cycles) = 0;
    virtual void onInput(const std::string& key, bool pressed) = 0;
};

class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;
    virtual void reset() = 0;
};

class IDisplayRenderer {
public:
    virtual ~IDisplayRenderer() = default;
    virtual void render(std::span<const std::uint8_t> vram) = 0;
};

class EmulatorRuntime {
public:
    explicit EmulatorRuntime(BrickConfig config);

    const BrickConfig& config() const;
    bool initialize();
    void reset();
    void runSteps(std::uint32_t steps, bool trace);

private:
    BrickConfig config_;
    std::unique_ptr<ICpuCore> cpu_;
};

} // namespace brickemu
