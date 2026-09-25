#include "brickemu/SPL03.hpp"

#include <iostream>
#include <stdexcept>

namespace brickemu {
namespace {

constexpr int SubClock = 32768;
constexpr std::uint8_t IntCfgT2Hz = 0x01;
constexpr std::uint8_t IntCfgT256Hz = 0x02;
constexpr std::uint8_t IntCfgPowerKey = 0x04;
constexpr std::uint8_t IntCfgNmiEnable = 0x80;
constexpr std::uint8_t IoCtrlRosc = 0x10;
constexpr std::uint8_t SystemCtrlCpuStop = 0x40;
constexpr std::uint8_t SystemCtrlRoscStop = 0x80;

} // namespace

SPL03::AudioCb SPL03::audioCb_ = nullptr;

void SPL03::emitSpeech() {
    if (!audioCb_) return;
    uint8_t data = speechData_;
    uint8_t ctrl = speechCtrl_;
    float amp = ((data & 0x3F) / 63.f) * (1 - ((data >> 6) & 0x02 ? 1 : 0));
    if (data & 0x80) amp = 0;
    double t = clockHz_ ? (double)cycleCounter_ / clockHz_ : 0;
    if (data == 0 || !(ctrl & 0x01)) audioCb_(3, 0, false, 0, t);
    else audioCb_(3, 0, false, amp, t);
}
void SPL03::emitToneA() {
    if (!audioCb_) return;
    float vol = (toneA2_ >> 6) / 3.f;
    int freq32 = (((toneA2_ & 0x3) << 8) | toneA1_) << 1;
    double t = clockHz_ ? (double)cycleCounter_ / clockHz_ : 0;
    if (vol > 0 && freq32 > 0) {
        float freq = freqFactor() * freq32;
        audioCb_(0, freq, false, vol, t);
    } else audioCb_(0, 0, false, 0, t);
}
void SPL03::emitNoise() {
    if (!audioCb_) return;
    float vol = (noise2_ >> 4) / 15.f;
    int freq32 = (16 - (noise1_ & 0xF)) << 7;
    double t = clockHz_ ? (double)cycleCounter_ / clockHz_ : 0;
    if (vol > 0 && freq32 > 0) {
        float freq = freqFactor() * freq32;
        audioCb_(2, freq, true, vol, t);
    } else audioCb_(2, 0, true, 0, t);
}

SPL03::SPL03(const BrickConfig& config)
    : clockHz_(config.clockHz),
      subClockDiv_(config.maskOptions.nonCrystalDiv) {
    RomError error;
    auto rom = Rom::load(config.romPath, &error);
    if (!rom) {
        throw std::runtime_error(error.message);
    }

    rom_ = std::move(*rom);
    if (subClockDiv_ == 0) {
        subClockDiv_ = static_cast<int>(clockHz_ / 32768U);
    }
    // Python parity: _pullup_ext defaults {PA:0,PB:0}, _port_pkey {PA:1,PB:0}
    const auto pullupIt = config.maskOptions.portPullup.find("PA");
    pullupPA_ = (pullupIt != config.maskOptions.portPullup.end())
        ? static_cast<std::uint8_t>(pullupIt->second) : 0;
    const auto pullupPb = config.maskOptions.portPullup.find("PB");
    pullupPB_ = (pullupPb != config.maskOptions.portPullup.end())
        ? static_cast<std::uint8_t>(pullupPb->second) : 0;
    const auto pkeyIt = config.maskOptions.portPkey.find("PA");
    pkeyPA_ = (pkeyIt != config.maskOptions.portPkey.end())
        ? static_cast<std::uint8_t>(pkeyIt->second) : 1;
    const auto pkeyPb = config.maskOptions.portPkey.find("PB");
    pkeyPB_ = (pkeyPb != config.maskOptions.portPkey.end())
        ? static_cast<std::uint8_t>(pkeyPb->second) : 0;

    reset();
}

void SPL03::reset() {
    ram_.fill(0);
    lcdRam_.fill(0);
    pc_ = 0;
    sp_ = 0;
    a_ = 0;
    x_ = 0;
    romBank_ = 0;
    setStatus(0x04);
    instructionCounter_ = 0;
    cycleCounter_ = 0;
    t2hzCounter_ = 0;
    t256hzCounter_ = 0;
    cpuEnabled_ = true;
    roscEnabled_ = true;
    prescalar_ = 1;
    systemCtrl_ = 0;
    intCfg_ = 0;
    ireq_ = 0;
    ioCtrl_ = 0;
    toneA1_ = toneA2_ = 0;
    noise1_ = noise2_ = 0;
    speechCtrl_ = speechData_ = 0;
    pdirPA_ = pdirPB_ = 0;
    platchPA_ = platchPB_ = 0;
    portIn0PA_ = portIn1PA_ = 0;
    portIn0PB_ = portIn1PB_ = 0;

    goVector(ResetVector);
}

std::uint32_t SPL03::clock() {
    // Python parity (SPL0X.clock): when ROSC is stopped only the 32K
    // sub-clock runs; when the CPU is stopped only timers advance.
    if (!roscEnabled_) {
        std::uint32_t cycles = 1; // MCLOCK_DIV
        if (systemCtrl_ & 0x20U) {
            cycles = static_cast<std::uint32_t>(subClockDiv_);
            timersClock(cycles);
        }
        cycleCounter_ += cycles;
        return cycles;
    }
    if (!cpuEnabled_) {
        const std::uint32_t cycles = 1; // MCLOCK_DIV
        timersClock(cycles);
        cycleCounter_ += cycles;
        return cycles;
    }

    const auto addr = effectivePc();
    const auto opcode = rom_.byte(addr);
    std::uint32_t bytes = 1;
    std::uint32_t cycles = 2;

    switch (opcode) {
    case 0x20: // JSR abs
    case 0x4C: // JMP abs
    case 0xBD: // LDA abs,X
        bytes = 3;
        break;
    case 0x08: // PHP
    case 0x18: // CLC
    case 0x2A: // ROL A
    case 0x38: // SEC
    case 0x40: // RTI
    case 0x48: // PHA
    case 0x58: // CLI
    case 0x60: // RTS
    case 0x68: // PLA
    case 0x6A: // ROR A
    case 0x78: // SEI
    case 0x88: // DEY (unused)
    case 0x9A: // TXS
    case 0xE8: // INX
        bytes = 1;
        break;
    case 0x05: // ORA zp
    case 0x09: // ORA #
    case 0x25: // AND zp
    case 0x29: // AND #
    case 0x49: // EOR #
    case 0x65: // ADC zp
    case 0x66: // ROR zp
    case 0x69: // ADC #
    case 0x81: // STA (ind,X)
    case 0x85: // STA zp
    case 0x86: // STX zp
    case 0x90: // BCC
    case 0x95: // STA zp,X
    case 0xA1: // LDA (ind,X)
    case 0xA5: // LDA zp
    case 0xA6: // LDX zp
    case 0xA2: // LDX #
    case 0xA9: // LDA #
    case 0xB0: // BCS
    case 0xB5: // LDA zp,X
    case 0xC5: // CMP zp
    case 0xC6: // DEC zp
    case 0xC9: // CMP #
    case 0xD0: // BNE
    case 0xE0: // CPX #
    case 0xE5: // SBC zp
    case 0xE6: // INC zp
    case 0xE9: // SBC #
    case 0xF0: // BEQ
        bytes = 2;
        break;
    default:
        // Covers 0x35: Python has no AND zp,X handler (_execute[0x35] is
        // 1-byte _dummy), so it must stay a 1-byte illegal instruction here.
        std::cerr << "illegal or unported SPL03 instruction pc=0x" << std::hex << pc_ << " opcode=0x" << static_cast<int>(opcode) << std::dec << '\n';
        bytes = 1;
        break;
    }

    // Read full instruction bytes (including opcode)
    const auto full_instruction = rom_.bytes(addr, bytes);
    pc_ = (pc_ + bytes) & 0xFFFFU;

    // Extract operand bytes (excluding opcode byte)
    // rom_.bytes() returns bytes in big-endian order within the integer:
    //   3-byte: [opcode, operand_lo, operand_hi] at bits [23:16], [15:8], [7:0]
    //   2-byte: [opcode, operand] at bits [15:8], [7:0]
    // For 3-byte instructions, handlers expect raw memory order (little-endian bytes as stored):
    //   operand = operand_lo | (operand_hi << 8) = low 16 bits of full_instruction
    // For 2-byte: operand at bits 7:0
    std::uint32_t operand = 0;
    if (bytes == 2) {
        operand = full_instruction & 0xFFU;
    } else if (bytes == 3) {
        operand = full_instruction & 0xFFFFU;
    }

    switch (opcode) {
    case 0x08: // PHP
        writeMem(sp_, status());
        sp_ = static_cast<std::uint8_t>(sp_ - 1U);
        cycles = 3;
        break;
    case 0x25: // AND zp
        a_ = static_cast<std::uint8_t>(a_ & readMem(operand & 0xFFU));
        setNz(a_);
        cycles = 3;
        break;
    case 0x29: // AND imm
        a_ = static_cast<std::uint8_t>(a_ & (operand & 0xFFU));
        setNz(a_);
        cycles = 2;
        break;
    case 0x20: { // JSR abs
        const auto returnPc = (pc_ - 1U) & 0xFFFFU;
        writeMem(sp_, static_cast<std::uint8_t>((returnPc >> 8U) & 0xFFU));
        sp_ = static_cast<std::uint8_t>(sp_ - 1U);
        writeMem(sp_, static_cast<std::uint8_t>(returnPc & 0xFFU));
        sp_ = static_cast<std::uint8_t>(sp_ - 1U);
        pc_ = ((operand >> 8U) & 0xFFU) | ((operand & 0xFFU) << 8U);
        cycles = 6;
        break;
    }
    case 0x40: // RTI
        sp_ = static_cast<std::uint8_t>(sp_ + 1U);
        setStatus(readMem(sp_));
        sp_ = static_cast<std::uint8_t>(sp_ + 1U);
        pc_ = readMem(sp_);
        sp_ = static_cast<std::uint8_t>(sp_ + 1U);
        pc_ |= static_cast<std::uint32_t>(readMem(sp_)) << 8U;
        cycles = 6;
        break;
    case 0x48: // PHA
        writeMem(sp_, a_);
        sp_ = static_cast<std::uint8_t>(sp_ - 1U);
        cycles = 3;
        break;
    case 0x4C: // JMP abs
        pc_ = ((operand & 0xFFU) << 8U) | ((operand >> 8U) & 0xFFU);
        cycles = 3;
        break;
    case 0x60: // RTS
        sp_ = static_cast<std::uint8_t>(sp_ + 1U);
        pc_ = readMem(sp_);
        sp_ = static_cast<std::uint8_t>(sp_ + 1U);
        pc_ |= static_cast<std::uint32_t>(readMem(sp_)) << 8U;
        pc_ = (pc_ + 1U) & 0xFFFFU;
        cycles = 6;
        break;
    case 0x66: { // ROR zp
        const auto address = operand & 0xFFU;
        const std::uint8_t prev = readMem(address);
        const std::uint8_t oldCf = cf_ ? 1U : 0U;
        writeMem(address, static_cast<std::uint8_t>((prev >> 1) | (oldCf << 7)));
        nf_ = (oldCf != 0); // Python parity: N = incoming carry
        zf_ = (((prev & 0xFEU) | oldCf) == 0);
        cf_ = (prev & 0x01U) != 0;
        cycles = 5;
        break;
    }
    case 0x68: // PLA
        sp_ = static_cast<std::uint8_t>(sp_ + 1U);
        a_ = readMem(sp_);
        setNz(a_);
        cycles = 4;
        break;
    case 0x6A: { // ROR A
        const std::uint8_t prev = a_;
        const std::uint8_t oldCf = cf_ ? 1U : 0U;
        a_ = static_cast<std::uint8_t>((prev >> 1) | (oldCf << 7));
        nf_ = (oldCf != 0); // Python parity: N = incoming carry
        zf_ = (((prev & 0xFEU) | oldCf) == 0);
        cf_ = (prev & 0x01U) != 0;
        cycles = 2;
        break;
    }
    case 0x78: // SEI
        if_ = true;
        cycles = 2;
        break;
    case 0x85: // STA zp
        writeMem(operand & 0xFFU, a_);
        cycles = 3;
        break;
    case 0x86: // STX zp
        writeMem(operand & 0xFFU, x_);
        cycles = 3;
        break;
    case 0x9A: // TXS
        sp_ = x_;
        cycles = 2;
        break;
    case 0xA2: // LDX imm
        x_ = static_cast<std::uint8_t>(operand & 0xFFU);
        setNz(x_);
        cycles = 2;
        break;
    case 0xA5: // LDA zp
        a_ = readMem(operand & 0xFFU);
        setNz(a_);
        cycles = 3;
        break;
    case 0xB5: // LDA zp,X
        a_ = readMem((operand + x_) & 0xFFU);
        setNz(a_);
        cycles = 4;
        break;
    case 0xA6: // LDX zp
        x_ = readMem(operand & 0xFFU);
        setNz(x_);
        cycles = 3;
        break;
    case 0xA9: // LDA imm
        a_ = static_cast<std::uint8_t>(operand & 0xFFU);
        setNz(a_);
        cycles = 2;
        break;
    case 0xD0: // BNE
        cycles = branchIf(!zf_, static_cast<std::uint8_t>(operand & 0xFFU));
        break;
    case 0xC6: { // DEC zp
        const auto address = operand & 0xFFU;
        const auto value = static_cast<std::uint8_t>(readMem(address) - 1U);
        writeMem(address, value);
        setNz(value);
        cycles = 5;
        break;
    }
    case 0xE6: { // INC zp
        const auto address = operand & 0xFFU;
        const auto value = static_cast<std::uint8_t>(readMem(address) + 1U);
        writeMem(address, value);
        setNz(value);
        cycles = 5;
        break;
    }
    case 0xE5: // SBC zp
        sbc(readMem(operand & 0xFFU));
        cycles = 3;
        break;
    case 0xE9: // SBC #
        sbc(static_cast<std::uint8_t>(operand & 0xFFU));
        cycles = 2;
        break;
    case 0xF0: // BEQ
        cycles = branchIf(zf_, static_cast<std::uint8_t>(operand & 0xFFU));
        break;
    case 0x05: // ORA zp
        a_ = static_cast<std::uint8_t>(a_ | readMem(operand & 0xFFU));
        setNz(a_);
        cycles = 3;
        break;
    case 0x09: // ORA #
        a_ = static_cast<std::uint8_t>(a_ | (operand & 0xFFU));
        setNz(a_);
        cycles = 2;
        break;
    case 0x18: // CLC
        cf_ = false;
        cycles = 2;
        break;
    case 0x38: // SEC
        cf_ = true;
        cycles = 2;
        break;
    case 0x58: // CLI
        if_ = false;
        cycles = 2;
        break;
    case 0x2A: // ROL A
        {
            const std::uint8_t old_cf = cf_ ? 1 : 0;
            cf_ = (a_ & 0x80U) != 0;
            a_ = static_cast<std::uint8_t>((a_ << 1) | old_cf);
            setNz(a_);
        }
        cycles = 2;
        break;
    case 0x49: // EOR #
        a_ = static_cast<std::uint8_t>(a_ ^ (operand & 0xFFU));
        setNz(a_);
        cycles = 2;
        break;
    case 0x65: // ADC zp
        adc(readMem(operand & 0xFFU));
        cycles = 3;
        break;
    case 0x69: // ADC #
        adc(static_cast<std::uint8_t>(operand & 0xFFU));
        cycles = 2;
        break;
    case 0x81: // STA (ind,X)
        {
            const std::uint16_t zp_addr = (operand + x_) & 0xFFU;
            const std::uint16_t addr = static_cast<std::uint16_t>(readMem(zp_addr)) | (static_cast<std::uint16_t>(readMem((zp_addr + 1) & 0xFFU)) << 8);
            writeMem(addr, a_);
        }
        cycles = 6;
        break;
    case 0x90: // BCC
        cycles = branchIf(!cf_, static_cast<std::uint8_t>(operand & 0xFFU));
        break;
    case 0x95: // STA zp,X
        writeMem((operand + x_) & 0xFFU, a_);
        cycles = 4;
        break;
    case 0xA1: // LDA (ind,X)
        {
            const std::uint16_t zp_addr = (operand + x_) & 0xFFU;
            const std::uint16_t addr = static_cast<std::uint16_t>(readMem(zp_addr)) | (static_cast<std::uint16_t>(readMem((zp_addr + 1) & 0xFFU)) << 8);
            a_ = readMem(addr);
            setNz(a_);
        }
        cycles = 6;
        break;
    case 0xB0: // BCS
        cycles = branchIf(cf_, static_cast<std::uint8_t>(operand & 0xFFU));
        break;
    case 0xBD: // LDA abs,X
        {
            // Extract 16-bit address from 3-byte instruction [opcode, lo_addr, hi_addr]
            // stored as little-endian: lo byte first, then hi byte
            const std::uint32_t baseAddr = ((operand & 0xFFU) << 8U) | ((operand >> 8) & 0xFFU);
            const std::uint32_t addr = (baseAddr + x_) & 0xFFFFU;
            a_ = readMem(addr);
            setNz(a_);
            // Python parity (SPL0X._lda_abs_x): page test is PC^addr, not base^addr.
            cycles = 4 + (((pc_ ^ addr) > 255U) ? 1U : 0U);
        }
        break;
    case 0xC5: // CMP zp
        {
            const std::uint8_t value = readMem(operand & 0xFFU);
            const std::uint8_t result = static_cast<std::uint8_t>(a_ - value);
            nf_ = (result & 0x80U) != 0;
            zf_ = (result == 0);
            cf_ = (a_ >= value);
        }
        cycles = 3;
        break;
    case 0xC9: // CMP #
        {
            const std::uint8_t value = static_cast<std::uint8_t>(operand & 0xFFU);
            const std::uint8_t result = static_cast<std::uint8_t>(a_ - value);
            nf_ = (result & 0x80U) != 0;
            zf_ = (result == 0);
            cf_ = (a_ >= value);
        }
        cycles = 2;
        break;
    case 0xE0: // CPX #
        {
            const std::uint8_t value = static_cast<std::uint8_t>(operand & 0xFFU);
            const std::uint8_t result = static_cast<std::uint8_t>(x_ - value);
            nf_ = (result & 0x80U) != 0;
            zf_ = (result == 0);
            cf_ = (x_ >= value);
        }
        cycles = 2;
        break;
    case 0xE8: // INX
        x_ = static_cast<std::uint8_t>(x_ + 1U);
        setNz(x_);
        cycles = 2;
        break;
    default:
        cycles = 2;
        break;
    }

    ++instructionCounter_;
    timersClock(cycles);
    cycleCounter_ += cycles;

    return cycles;
}

std::uint32_t SPL03::pc() const {
    return pc_;
}

std::span<const std::uint8_t> SPL03::vram() const {
    return lcdRam_;
}

CpuSnapshot SPL03::snapshot() const {
    return CpuSnapshot{
        .pc = effectivePc(),
        .opcode = rom_.byte(effectivePc()),
        .a = a_,
        .x = x_,
        .sp = sp_,
        .nf = nf_,
        .vf = vf_,
        .df = df_,
        .bf = bf_,
        .interruptFlag = if_,
        .zf = zf_,
        .cf = cf_,
    };
}

std::uint32_t SPL03::instructionCounter() const {
    return instructionCounter_;
}

std::uint32_t SPL03::cycleCounter() const {
    return cycleCounter_;
}

std::uint32_t SPL03::romSize() const {
    return rom_.size();
}

std::uint32_t SPL03::romMask() const {
    return rom_.mask();
}

std::uint32_t SPL03::effectivePc() const {
    if (pc_ > 0x0FFFU) {
        return (pc_ % AddressSpaceSize) + (static_cast<std::uint32_t>(romBank_) << 12U);
    }
    return pc_;
}

void SPL03::goVector(std::uint32_t address) {
    pc_ = rom_.wordLsb(address + (static_cast<std::uint32_t>(romBank_) << 12U));
}

std::uint8_t SPL03::readMem(std::uint32_t address) const {
    address &= 0xFFFFU;
    if (address < LcdRamSize) {
        return lcdRam_[address];
    }
    if (address >= 0x30U && address < 0x30U + CpuRamSize) {
        return ram_[address - 0x30U];
    }
    if (address >= 0xC0U && address < 0x100U) {
        // SFR map (Python parity with SPL03._io_tbl); unmapped SFR reads 0.
        switch (address) {
        case 0xC0: return ioCtrl_;
        case 0xC1: return portRead(true);
        case 0xC3: return portRead(false);
        case 0xC4: // ToneA Ctrl1 (write-only in Python)
        case 0xC6: // ToneA Ctrl2
        case 0xCC: // Noise Ctrl1
        case 0xCE: // Noise Ctrl2
            return 0;
        case 0xD0: return systemCtrl_;
        case 0xD2: {
            const auto value = static_cast<std::uint8_t>(ireq_ | (intCfg_ & IntCfgNmiEnable));
            // The Python reference clears IREQ on read.
            const_cast<SPL03*>(this)->ireq_ = 0;
            return value;
        }
        case 0xD4: return speechCtrl_;
        case 0xD5: return (cycleCounter_ >> 3) & 0x1;
        case 0xD7: return romBank_;
        default: return 0;
        }
    }
    if (address >= 0x1000U) {
        address = (address % AddressSpaceSize) + (static_cast<std::uint32_t>(romBank_) << 12U);
    }
    return rom_.byte(address);
}

void SPL03::writeMem(std::uint32_t address, std::uint8_t value) {
    address &= 0xFFFFU;
    if (address < LcdRamSize) {
        lcdRam_[address] = value;
        return;
    }
    if (address >= 0x30U && address < 0x30U + CpuRamSize) {
        ram_[address - 0x30U] = value;
        return;
    }
    switch (address) {
    case 0xC0: setIoCtrl(value); break;
    case 0xC1: platchPA_ = value; break;
    case 0xC3: platchPB_ = value; break;
    case 0xC4: toneA1_ = value; emitToneA(); break;
    case 0xC6: toneA2_ = value; emitToneA(); break;
    case 0xCC: noise1_ = value; emitNoise(); break;
    case 0xCE: noise2_ = value; emitNoise(); break;
    case 0xD0:
        roscEnabled_ = ((systemCtrl_ | static_cast<std::uint8_t>(~value)) & SystemCtrlRoscStop) != 0;
        cpuEnabled_ = ((systemCtrl_ | static_cast<std::uint8_t>(~value)) & SystemCtrlCpuStop) != 0;
        systemCtrl_ = value;
        break;
    case 0xD2: intCfg_ = value; break;
    case 0xD4: speechCtrl_ = value; emitSpeech(); break;
    case 0xD5: speechData_ = static_cast<std::uint8_t>(value & 0xBEU); emitSpeech(); break; // SPL03 masks D5 writes
    case 0xD7: romBank_ = static_cast<std::uint8_t>(value & 0x1U); break; // SPL03: 1 bank bit
    default: break; // Unmapped SFR and ROM space: writes vanish (Python parity)
    }
}

void SPL03::adc(std::uint8_t operand) {
    // Verbatim Python parity (SPL0X._adc), including the BCD adjust when DF is set.
    const std::uint8_t a = a_;
    const std::uint8_t cin = cf_ ? 1 : 0;
    int result = static_cast<int>(a) + operand + cin;
    if (df_ && ((a & 0x0F) + (operand & 0x0F) + cin > 9)) {
        result += 6;
    }
    vf_ = ((~(a ^ operand) & (a ^ result)) >> 7) & 1;
    nf_ = (result >> 7) & 1;
    if (df_ && (result > 0x99)) {
        result += 0x60;
    }
    zf_ = ((result & 0xFF) == 0);
    cf_ = (result > 255);
    a_ = static_cast<std::uint8_t>(result & 0xFF);
}

void SPL03::sbc(std::uint8_t operand) {
    // Verbatim Python parity (SPL0X._sbc), including the BCD adjust when DF is set.
    const std::uint8_t a = a_;
    const int borrow = cf_ ? 0 : 1;
    int result = static_cast<int>(a) - operand - borrow;
    if (df_) {
        if ((static_cast<int>(a & 0x0F) - (operand & 0x0F) - borrow) < 0) {
            result -= 6;
        }
        if (result < 0) {
            result -= 0x60;
        }
    }
    vf_ = (((a ^ operand) & (a ^ result)) >> 7) & 1;
    nf_ = (result >> 7) & 1;
    zf_ = ((result & 0xFF) == 0);
    cf_ = (result >= 0);
    a_ = static_cast<std::uint8_t>(result & 0xFF);
}

std::uint8_t SPL03::portRead(bool portA) const {
    // Python parity (SPL0X._port_read).
    const std::uint8_t pdir = portA ? pdirPA_ : pdirPB_;
    const std::uint8_t latch = portA ? platchPA_ : platchPB_;
    const std::uint8_t pullup = portA ? pullupPA_ : pullupPB_;
    const std::uint8_t in0 = portA ? portIn0PA_ : portIn0PB_;
    const std::uint8_t in1 = portA ? portIn1PA_ : portIn1PB_;
    return static_cast<std::uint8_t>(
        ((static_cast<std::uint8_t>(~pdir) & latch) |
         (pdir & (static_cast<std::uint8_t>(~in0) & (in1 | pullup)))) &
        0xFFU);
}

void SPL03::setPortInput(const std::string& port, std::uint8_t mask, int level) {
    // Python parity (SPL0X.port_handler).
    if (port == "RES") {
        if (level == 0) {
            reset();
        }
        return;
    }
    const bool portA = (port == "PA");
    const std::uint8_t prev = portRead(portA);
    if (portA) {
        portIn0PA_ &= static_cast<std::uint8_t>(~mask);
        portIn1PA_ &= static_cast<std::uint8_t>(~mask);
        if (level >= 0) {
            (level == 0 ? portIn0PA_ : portIn1PA_) |= mask;
        }
    } else {
        portIn0PB_ &= static_cast<std::uint8_t>(~mask);
        portIn1PB_ &= static_cast<std::uint8_t>(~mask);
        if (level >= 0) {
            (level == 0 ? portIn0PB_ : portIn1PB_) |= mask;
        }
    }
    const std::uint8_t pkey = portA ? pkeyPA_ : pkeyPB_;
    if ((prev & pkey) < (portRead(portA) & pkey)) {
        if (intCfg_ & IntCfgPowerKey) {
            ireq_ |= IntCfgPowerKey;
            nmi();
        }
    }
}

void SPL03::setIoCtrl(std::uint8_t value) {
    // Python parity (SPL0X._set_io_IO_Ctrl).
    if (value & 0x01U) {
        pdirPA_ |= 0x0FU;
    }
    if (value & 0x02U) {
        pdirPA_ |= 0xF0U;
    }
    if (value & 0x04U) {
        pdirPB_ |= 0x03U;
    }
    roscEnabled_ = ((ioCtrl_ | static_cast<std::uint8_t>(~value)) & IoCtrlRosc) != 0;
    prescalar_ = ((value & 0x20U) != 0) ? 8 : 1;
    ioCtrl_ = value;
}

std::uint8_t SPL03::status() const {
    return static_cast<std::uint8_t>((nf_ << 7U) | (vf_ << 6U) | (bf_ << 4U) | (df_ << 3U) | (if_ << 2U) | (zf_ << 1U) | cf_);
}

void SPL03::setStatus(std::uint8_t status) {
    nf_ = (status & 0x80U) != 0;
    vf_ = (status & 0x40U) != 0;
    bf_ = (status & 0x10U) != 0;
    df_ = (status & 0x08U) != 0;
    if_ = (status & 0x04U) != 0;
    zf_ = (status & 0x02U) != 0;
    cf_ = (status & 0x01U) != 0;
}

void SPL03::setNz(std::uint8_t value) {
    nf_ = (value & 0x80U) != 0;
    zf_ = value == 0;
}

std::uint32_t SPL03::branchIf(bool condition, std::uint8_t offset) {
    if (!condition) {
        return 2;
    }
    const auto previous = pc_;
    pc_ = (pc_ + offset - ((offset & 0x80U) << 1U)) & 0xFFFFU;
    return 3U + (((pc_ ^ previous) > 255U) ? 1U : 0U);
}

void SPL03::timersClock(std::uint32_t cycles) {
    t2hzCounter_ -= static_cast<int>(cycles);
    while (t2hzCounter_ <= 0) {
        t2hzCounter_ += subClockDiv_ * (SubClock / 2);
        if (intCfg_ & IntCfgT2Hz) {
            ireq_ |= IntCfgT2Hz;
            nmi();
        }
    }

    t256hzCounter_ -= static_cast<int>(cycles);
    while (t256hzCounter_ <= 0) {
        t256hzCounter_ += subClockDiv_ * (SubClock / 256);
        if (intCfg_ & IntCfgT256Hz) {
            ireq_ |= IntCfgT256Hz;
            nmi();
        }
    }
}

void SPL03::nmi() {
    if ((intCfg_ & IntCfgNmiEnable) == 0) {
        return;
    }
    if (roscEnabled_ && cpuEnabled_) {
        writeMem(sp_, static_cast<std::uint8_t>((pc_ >> 8U) & 0xFFU));
        sp_ = static_cast<std::uint8_t>(sp_ - 1U);
        writeMem(sp_, static_cast<std::uint8_t>(pc_ & 0xFFU));
        sp_ = static_cast<std::uint8_t>(sp_ - 1U);
        writeMem(sp_, status());
        sp_ = static_cast<std::uint8_t>(sp_ - 1U);
        goVector(NmiVector);
    } else {
        cpuEnabled_ = true;
        roscEnabled_ = true;
        goVector(ResetVector);
    }
}

} // namespace brickemu
