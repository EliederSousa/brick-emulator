#include "brickemu/EmulatorRuntime.hpp"

#include "brickemu/CoreRegistry.hpp"

#include <iostream>
#include <iomanip>

namespace brickemu {

EmulatorRuntime::EmulatorRuntime(BrickConfig config)
    : config_(std::move(config)) {}

const BrickConfig& EmulatorRuntime::config() const {
    return config_;
}

bool EmulatorRuntime::initialize() {
    std::cout << "Initializing BrickEmu runtime\n";
    std::cout << "  core: " << config_.core << '\n';
    std::cout << "  clock: " << config_.clockHz << " Hz\n";
    std::cout << "  rom: " << config_.romPath << '\n';

    std::string error;
    try {
        cpu_ = CoreRegistry::create(config_, &error);
    } catch (const std::exception& ex) {
        std::cerr << "failed to create core: " << ex.what() << '\n';
        return false;
    }

    if (!cpu_) {
        std::cerr << error << '\n';
        return false;
    }

    std::cout << "  rom bytes: " << cpu_->romSize() << '\n';
    std::cout << "  rom mask: 0x" << std::hex << cpu_->romMask() << std::dec << '\n';
    std::cout << "  reset pc: 0x" << std::hex << cpu_->pc() << std::dec << '\n';
    std::cout << "  vram bytes: " << cpu_->vram().size() << '\n';
    return true;
}

void EmulatorRuntime::reset() {
    if (cpu_) {
        cpu_->reset();
    }
}

void EmulatorRuntime::runSteps(std::uint32_t steps, bool trace) {
    if (!cpu_) {
        return;
    }

    for (std::uint32_t i = 0; i < steps; ++i) {
        if (trace) {
            const auto s = cpu_->snapshot();
            std::cout << std::setfill('0')
                      << std::setw(2) << std::dec << i
                      << " pc=" << std::uppercase << std::hex << std::setw(4) << s.pc
                      << " op=" << std::setw(2) << static_cast<int>(s.opcode)
                      << " A=" << std::setw(2) << static_cast<int>(s.a)
                      << " X=" << std::setw(2) << static_cast<int>(s.x)
                      << " SP=" << std::setw(2) << static_cast<int>(s.sp)
                      << std::nouppercase << std::dec
                      << " NF=" << s.nf
                      << " VF=" << s.vf
                      << " DF=" << s.df
                      << " BF=" << s.bf
                      << " IF=" << s.interruptFlag
                      << " ZF=" << s.zf
                      << " CF=" << s.cf
                      << '\n';
        }
        cpu_->clock();
    }

    if (steps > 0 && !trace) {
        const auto s = cpu_->snapshot();
        std::cout << "C++ step " << steps << ": pc=" << std::uppercase << std::hex << std::setw(4) << s.pc
                  << " A=" << std::setw(2) << static_cast<int>(s.a)
                  << " X=" << std::setw(2) << static_cast<int>(s.x)
                  << " SP=" << std::setw(2) << static_cast<int>(s.sp)
                  << std::nouppercase << std::dec
                  << " NF=" << s.nf
                  << " ZF=" << s.zf
                  << " CF=" << s.cf
                  << " IF=" << s.interruptFlag
                  << " cyc=" << cpu_->instructionCounter()
                  << " total_cyc=" << cpu_->cycleCounter()
                  << '\n';
    }
}

} // namespace brickemu
