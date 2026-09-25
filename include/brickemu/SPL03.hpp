#pragma once

#include "brickemu/Config.hpp"
#include "brickemu/EmulatorRuntime.hpp"
#include "brickemu/Rom.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace brickemu {

class SPL03 final : public ICpuCore {
public:
    static constexpr std::uint32_t AddressSpaceSize = 0x2000;
    static constexpr std::uint32_t NmiVector = 0x1FFA;
    static constexpr std::uint32_t ResetVector = 0x1FFC;
    static constexpr std::uint32_t CpuRamSize = 0x50;
    static constexpr std::uint32_t LcdRamSize = 0x30;

    explicit SPL03(const BrickConfig& config);

    void reset() override;
    std::uint32_t clock() override;
    std::uint32_t pc() const override;
    std::uint32_t romSize() const override;
    std::uint32_t romMask() const override;
    std::span<const std::uint8_t> vram() const override;
    CpuSnapshot snapshot() const override;

    std::uint32_t instructionCounter() const;
    std::uint32_t cycleCounter() const;
    void setPortInput(const std::string& port, std::uint8_t mask, int level) override;
    // sound registers (for SDL audio)
    std::uint8_t toneA1() const { return toneA1_; }
    std::uint8_t toneA2() const { return toneA2_; }
    std::uint8_t noise1() const { return noise1_; }
    std::uint8_t noise2() const { return noise2_; }
    std::uint8_t speechCtrl() const { return speechCtrl_; }
    std::uint8_t speechData() const { return speechData_; }
    float freqFactor() const { return clockHz_ > 0 && subClockDiv_ > 0 ? (float)clockHz_ / subClockDiv_ / 32768.f : 1.f; }
    using AudioCb = std::function<void(int, float, bool, float, double)>;
    static void setAudioCallback(AudioCb cb) { audioCb_ = std::move(cb); }

private:
    std::uint32_t effectivePc() const;
    void goVector(std::uint32_t address);
    std::uint8_t readMem(std::uint32_t address) const;
    void writeMem(std::uint32_t address, std::uint8_t value);
    std::uint8_t status() const;
    void setStatus(std::uint8_t status);
    void setNz(std::uint8_t value);
    void adc(std::uint8_t operand); // Python-parity ADC incl. BCD adjust when DF set
    void sbc(std::uint8_t operand); // Python-parity SBC incl. BCD adjust when DF set
    std::uint8_t portRead(bool portA) const;
    void setIoCtrl(std::uint8_t value);
    std::uint32_t branchIf(bool condition, std::uint8_t offset);
    void timersClock(std::uint32_t cycles);
    void nmi();

    Rom rom_;
    std::array<std::uint8_t, CpuRamSize> ram_{};
    std::array<std::uint8_t, LcdRamSize> lcdRam_{};
    std::uint32_t pc_ = 0;
    std::uint8_t sp_ = 0;
    std::uint8_t a_ = 0;
    std::uint8_t x_ = 0;
    std::uint8_t romBank_ = 0;
    bool nf_ = false;
    bool vf_ = false;
    bool df_ = false;
    bool bf_ = false;
    bool if_ = true;
    bool zf_ = false;
    bool cf_ = false;
    std::uint32_t instructionCounter_ = 0;
    std::uint32_t cycleCounter_ = 0;
    std::uint32_t clockHz_ = 0;
    int subClockDiv_ = 0;
    int t2hzCounter_ = 0;
    int t256hzCounter_ = 0;
    bool cpuEnabled_ = true;
    bool roscEnabled_ = true;
    int prescalar_ = 1;
    std::uint8_t systemCtrl_ = 0;
    std::uint8_t intCfg_ = 0;
    std::uint8_t ireq_ = 0;
    // SFR state (Python parity with SPL0X/SPL03 _io_tbl)
    std::uint8_t ioCtrl_ = 0;
    std::uint8_t toneA1_ = 0;
    std::uint8_t toneA2_ = 0;
    std::uint8_t noise1_ = 0;
    std::uint8_t noise2_ = 0;
    std::uint8_t speechCtrl_ = 0;
    std::uint8_t speechData_ = 0;
    std::uint8_t pdirPA_ = 0;
    std::uint8_t pdirPB_ = 0;
    std::uint8_t platchPA_ = 0;
    std::uint8_t platchPB_ = 0;
    std::uint8_t pullupPA_ = 0;
    std::uint8_t pullupPB_ = 0;
    std::uint8_t pkeyPA_ = 1;
    std::uint8_t pkeyPB_ = 0;
    std::uint8_t portIn0PA_ = 0; // driven-low bits (level 0)
    std::uint8_t portIn1PA_ = 0; // driven-high bits (level 1)
    std::uint8_t portIn0PB_ = 0;
    std::uint8_t portIn1PB_ = 0;
    static AudioCb audioCb_;
    void emitSpeech();
    void emitToneA();
    void emitNoise();
};

} // namespace brickemu
