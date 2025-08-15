#include "ur/assembler/arm64-v8a/assembler.h"
#include <stdexcept>
#include <cmath>

namespace ur::assembler::arm64 {

namespace {
    bool is_w_register(Register reg) {
        return reg >= Register::W0 && reg <= Register::WZR;
    }
    bool is_x_register(Register reg) {
        return (reg >= Register::X0 && reg <= Register::ZR) || reg == Register::SP;
    }
    bool is_s_register(Register reg) {
        return reg >= Register::S0 && reg <= Register::S31;
    }
    bool is_d_register(Register reg) {
        return reg >= Register::D0 && reg <= Register::D31;
    }
    bool is_q_register(Register reg) {
        return reg >= Register::Q0 && reg <= Register::Q31;
    }
    bool is_fp_register(Register reg) {
        return is_s_register(reg) || is_d_register(reg) || is_q_register(reg);
    }

    uint32_t to_sys_reg(SystemRegister sys_reg) {
        // This encoding is a bit complex. It's composed of op0, op1, CRn, CRm, op2 fields.
        // Let's represent them as a single value for simplicity here.
        // The format is: op0:2, op1:3, CRn:4, CRm:4, op2:3
        switch (sys_reg) {
            case SystemRegister::NZCV:
                // op0=1, op1=3, CRn=4, CRm=2, op2=0 -> 1_011_0100_0010_000
                return (1 << 19) | (3 << 16) | (4 << 12) | (2 << 8) | (0 << 5);
            case SystemRegister::FPCR:
                // op0=1, op1=3, CRn=4, CRm=4, op2=0 -> 1_011_0100_0100_000
                return (1 << 19) | (3 << 16) | (4 << 12) | (4 << 8) | (0 << 5);
            case SystemRegister::FPSR:
                 // op0=1, op1=3, CRn=4, CRm=4, op2=1 -> 1_011_0100_0100_001
                return (1 << 19) | (3 << 16) | (4 << 12) | (4 << 8) | (1 << 5);
            case SystemRegister::TPIDR_EL0:
                // op0=1, op1=3, CRn=13, CRm=0, op2=2 -> 1_011_1101_0000_010
                return (1 << 19) | (3 << 16) | (13 << 12) | (0 << 8) | (2 << 5);
        }
        throw std::runtime_error("Unsupported system register");
    }
}

AssemblerAArch64::AssemblerAArch64(uintptr_t start_address) : current_address_(start_address) {}

void AssemblerAArch64::emit(uint32_t instruction) {
    code_.push_back(instruction);
    current_address_ += 4;
}

uint32_t AssemblerAArch64::to_reg(Register reg) {
    if (reg == Register::ZR || reg == Register::WZR) return 31;
    if (is_w_register(reg)) {
        return static_cast<uint32_t>(reg) - static_cast<uint32_t>(Register::W0);
    }
    if (is_s_register(reg)) {
        return static_cast<uint32_t>(reg) - static_cast<uint32_t>(Register::S0);
    }
    if (is_d_register(reg)) {
        return static_cast<uint32_t>(reg) - static_cast<uint32_t>(Register::D0);
    }
    if (is_q_register(reg)) {
        return static_cast<uint32_t>(reg) - static_cast<uint32_t>(Register::Q0);
    }
    return static_cast<uint32_t>(reg);
}

uint32_t AssemblerAArch64::to_cond(Condition cond) {
    return static_cast<uint32_t>(cond);
}

void AssemblerAArch64::gen_abs_jump(uintptr_t destination, Register reg) {
    movz(reg, destination & 0xFFFF, 0);
    movk(reg, (destination >> 16) & 0xFFFF, 16);
    movk(reg, (destination >> 32) & 0xFFFF, 32);
    movk(reg, (destination >> 48) & 0xFFFF, 48);
    br(reg);
}

void AssemblerAArch64::gen_abs_call(uintptr_t destination, Register reg) {
    stp(Register::FP, Register::LR, Register::SP, -16, true);
    movz(reg, destination & 0xFFFF, 0);
    movk(reg, (destination >> 16) & 0xFFFF, 16);
    movk(reg, (destination >> 32) & 0xFFFF, 32);
    movk(reg, (destination >> 48) & 0xFFFF, 48);
    blr(reg);
    ldp(Register::FP, Register::LR, Register::SP, 16, true);
}

void AssemblerAArch64::gen_load_address(Register dest, uintptr_t address) {
    mov(dest, address);
}

void AssemblerAArch64::b(uintptr_t target_address) {
    int64_t offset = target_address - current_address_;
    if (offset < -134217728 || offset > 134217724) throw std::runtime_error("Branch offset out of range");
    uint32_t imm26 = (static_cast<uint32_t>(offset) >> 2) & 0x3FFFFFF;
    emit(0x14000000 | imm26);
}

void AssemblerAArch64::b(Condition cond, uintptr_t target_address) {
    int64_t offset = target_address - current_address_;
    if (offset < -524288 || offset > 524284) throw std::runtime_error("Conditional branch offset out of range");
    uint32_t imm19 = (static_cast<uint32_t>(offset) >> 2) & 0x7FFFF;
    emit(0x54000000 | (imm19 << 5) | to_cond(cond));
}

void AssemblerAArch64::bl(uintptr_t target_address) {
    int64_t offset = target_address - current_address_;
    if (offset < -134217728 || offset > 134217724) throw std::runtime_error("Branch with link offset out of range");
    uint32_t imm26 = (static_cast<uint32_t>(offset) >> 2) & 0x3FFFFFF;
    emit(0x94000000 | imm26);
}

void AssemblerAArch64::blr(Register reg) {
    emit(0xD63F0000 | (to_reg(reg) << 5));
}

void AssemblerAArch64::br(Register reg) {
    emit(0xD61F0000 | (to_reg(reg) << 5));
}

void AssemblerAArch64::ret() {
    emit(0xD65F03C0); // RET is an alias for BR LR
}

void AssemblerAArch64::cbz(Register rt, uintptr_t target_address) {
    int64_t offset = target_address - current_address_;
    if (offset < 0 || offset > 524284) throw std::runtime_error("CBZ offset out of range");
    uint32_t imm19 = (static_cast<uint32_t>(offset) >> 2) & 0x7FFFF;
    emit(0xB4000000 | (imm19 << 5) | to_reg(rt));
}

void AssemblerAArch64::cbnz(Register rt, uintptr_t target_address) {
    int64_t offset = target_address - current_address_;
    if (offset < 0 || offset > 524284) throw std::runtime_error("CBNZ offset out of range");
    uint32_t imm19 = (static_cast<uint32_t>(offset) >> 2) & 0x7FFFF;
    emit(0xB5000000 | (imm19 << 5) | to_reg(rt));
}

void AssemblerAArch64::tbz(Register rt, uint32_t bit, uintptr_t target_address) {
    if (bit >= (is_w_register(rt) ? 32 : 64)) throw std::runtime_error("TBZ bit out of range");
    int64_t offset = target_address - current_address_;
    if (offset < -32768 || offset > 32767) throw std::runtime_error("TBZ offset out of range");
    uint32_t b5 = (bit >> 5) & 1;
    uint32_t b40 = bit & 0x1F;
    uint32_t imm14 = (static_cast<uint32_t>(offset) >> 2) & 0x3FFF;
    emit(0x36000000 | (b5 << 31) | (b40 << 19) | (imm14 << 5) | to_reg(rt));
}

void AssemblerAArch64::tbnz(Register rt, uint32_t bit, uintptr_t target_address) {
    if (bit >= (is_w_register(rt) ? 32 : 64)) throw std::runtime_error("TBNZ bit out of range");
    int64_t offset = target_address - current_address_;
    if (offset < -32768 || offset > 32767) throw std::runtime_error("TBNZ offset out of range");
    uint32_t b5 = (bit >> 5) & 1;
    uint32_t b40 = bit & 0x1F;
    uint32_t imm14 = (static_cast<uint32_t>(offset) >> 2) & 0x3FFF;
    emit(0x37000000 | (b5 << 31) | (b40 << 19) | (imm14 << 5) | to_reg(rt));
}

void AssemblerAArch64::add(Register rd, Register rn, uint16_t imm, bool shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    uint32_t sh = shift ? 1 : 0;
    emit((sf << 31) | 0x11000000 | (sh << 22) | (static_cast<uint32_t>(imm) << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::sub(Register rd, Register rn, uint16_t imm, bool shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    uint32_t sh = shift ? 1 : 0;
    emit((sf << 31) | 0x51000000 | (sh << 22) | (static_cast<uint32_t>(imm) << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::and_(Register rd, Register rn, uint64_t bitmask) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    uint32_t n, imms, immr;
    if (!try_encode_logical_imm(bitmask, n, imms, immr)) throw std::runtime_error("Invalid bitmask for AND");
    if (sf == 0 && n != 0) throw std::runtime_error("Invalid bitmask for 32-bit AND");
    emit((sf << 31) | 0x12000000 | (n << 22) | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::orr(Register rd, Register rn, uint64_t bitmask) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    uint32_t n, imms, immr;
    if (!try_encode_logical_imm(bitmask, n, imms, immr)) throw std::runtime_error("Invalid bitmask for ORR");
    if (sf == 0 && n != 0) throw std::runtime_error("Invalid bitmask for 32-bit ORR");
    emit((sf << 31) | 0x32000000 | (n << 22) | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::eor(Register rd, Register rn, uint64_t bitmask) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    uint32_t n, imms, immr;
    if (!try_encode_logical_imm(bitmask, n, imms, immr)) throw std::runtime_error("Invalid bitmask for EOR");
    if (sf == 0 && n != 0) throw std::runtime_error("Invalid bitmask for 32-bit EOR");
    emit((sf << 31) | 0x52000000 | (n << 22) | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::mov(Register rd, uint64_t imm) {
    // Assembles to MOVZ/MOVK
    bool first_mov = true;
    int limit = is_w_register(rd) ? 32 : 64;
    for (int shift = 0; shift < limit; shift += 16) {
        uint16_t chunk = (imm >> shift) & 0xFFFF;
        if (chunk != 0) {
            if (first_mov) {
                movz(rd, chunk, shift);
                first_mov = false;
            } else {
                movk(rd, chunk, shift);
            }
        }
    }
    if (first_mov) { // If imm is 0
        movz(rd, 0, 0);
    }
}

void AssemblerAArch64::mov(Register rd, Register rn) {
    if (rn == Register::SP || rn == Register::WSP) {
        // MOV (register) from SP is an alias for ADD (immediate) with imm=0.
        add(rd, rn, 0);
    } else {
        // For other GPRs, MOV is an alias for ORR with the zero register.
        orr(rd, is_w_register(rd) ? Register::WZR : Register::ZR, rn);
    }
}

void AssemblerAArch64::movn(Register rd, uint16_t imm, int shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x12800000 | ((shift / 16) << 21) | (uint32_t)imm << 5 | to_reg(rd));
}

void AssemblerAArch64::movz(Register rd, uint16_t imm, int shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x52800000 | ((shift / 16) << 21) | (uint32_t)imm << 5 | to_reg(rd));
}

void AssemblerAArch64::movk(Register rd, uint16_t imm, int shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x72800000 | ((shift / 16) << 21) | (uint32_t)imm << 5 | to_reg(rd));
}

void AssemblerAArch64::add(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x0B000000 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::sub(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x4B000000 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::and_(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x0A000000 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::orr(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x2A000000 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::eor(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x4A000000 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::bic(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x0A200000 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::mvn(Register rd, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    Register zr = sf ? Register::ZR : Register::WZR;
    // MVN rd, rm is an alias for ORN rd, zr, rm
    emit((sf << 31) | 0x2A200000 | (to_reg(rm) << 16) | (to_reg(zr) << 5) | to_reg(rd));
}

void AssemblerAArch64::lsl(Register rd, Register rn, uint32_t shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    unsigned reg_size = sf ? 64 : 32;
    if (shift >= reg_size) throw std::runtime_error("LSL shift amount out of range");
    uint32_t immr = (reg_size - shift) % reg_size;
    uint32_t imms = reg_size - 1 - shift;
    // LSL is an alias for UBFM
    emit((sf << 31) | (sf << 22) | 0x53000000 | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::lsr(Register rd, Register rn, uint32_t shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    unsigned reg_size = sf ? 64 : 32;
    if (shift >= reg_size) throw std::runtime_error("LSR shift amount out of range");
    uint32_t immr = shift;
    uint32_t imms = reg_size - 1;
    // LSR is an alias for UBFM
    emit((sf << 31) | (sf << 22) | 0x53000000 | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::asr(Register rd, Register rn, uint32_t shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    unsigned reg_size = sf ? 64 : 32;
    if (shift >= reg_size) throw std::runtime_error("ASR shift amount out of range");
    uint32_t immr = shift;
    uint32_t imms = reg_size - 1;
    // ASR is an alias for SBFM
    emit((sf << 31) | (sf << 22) | 0x13000000 | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::cmp(Register rn, Register rm) {
    sub(is_w_register(rn) ? Register::WZR : Register::ZR, rn, rm);
}

void AssemblerAArch64::cset(Register rd, Condition cond) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    Condition inverted_cond = static_cast<Condition>(static_cast<uint32_t>(cond) ^ 1);
    emit((sf << 31) | 0x1A800400 | to_reg(rd) | (to_cond(inverted_cond) << 12) | (to_reg(is_w_register(rd) ? Register::WZR : Register::ZR) << 5) | (to_reg(is_w_register(rd) ? Register::WZR : Register::ZR) << 16));
}

void AssemblerAArch64::csel(Register rd, Register rn, Register rm, Condition cond) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x1A800000 | (to_reg(rm) << 16) | (to_cond(cond) << 12) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::csinc(Register rd, Register rn, Register rm, Condition cond) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x1A800400 | (to_reg(rm) << 16) | (to_cond(cond) << 12) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::csinv(Register rd, Register rn, Register rm, Condition cond) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x5A800000 | (to_reg(rm) << 16) | (to_cond(cond) << 12) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::csneg(Register rd, Register rn, Register rm, Condition cond) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x5A800400 | (to_reg(rm) << 16) | (to_cond(cond) << 12) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::mul(Register rd, Register rn, Register rm) {
    madd(rd, rn, rm, is_w_register(rd) ? Register::WZR : Register::ZR);
}

void AssemblerAArch64::sdiv(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x1AC00C00 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::udiv(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x1AC00800 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::madd(Register rd, Register rn, Register rm, Register ra) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x1B000000 | (to_reg(rm) << 16) | (to_reg(ra) << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::msub(Register rd, Register rn, Register rm, Register ra) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x1B008000 | (to_reg(rm) << 16) | (to_reg(ra) << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::fadd(Register rd, Register rn, Register rm) {
    uint32_t type = is_d_register(rd) ? 1 : 0;
    emit(0x1E202800 | (type << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::fsub(Register rd, Register rn, Register rm) {
    uint32_t type = is_d_register(rd) ? 1 : 0;
    emit(0x1E203800 | (type << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::fmul(Register rd, Register rn, Register rm) {
    uint32_t type = is_d_register(rd) ? 1 : 0;
    emit(0x1E200800 | (type << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::fdiv(Register rd, Register rn, Register rm) {
    uint32_t type = is_d_register(rd) ? 1 : 0;
    emit(0x1E201800 | (type << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::fmov(Register dest, Register src) {
    bool dest_is_fp = is_fp_register(dest);
    bool src_is_fp = is_fp_register(src);

    if (dest_is_fp && src_is_fp) {
        if ((is_s_register(dest) && !is_s_register(src)) || (is_d_register(dest) && !is_d_register(src)) || (is_q_register(dest) && !is_q_register(src))) {
            throw std::runtime_error("FMOV between different size float registers not supported directly.");
        }
        uint32_t type = is_d_register(dest) ? 1 : 0;
        emit(0x1E204000 | (type << 22) | (to_reg(src) << 5) | to_reg(dest));
    } else if (dest_is_fp && !src_is_fp) { // GPR to FPR
        if (is_d_register(dest) && !is_x_register(src)) throw std::runtime_error("FMOV to D register requires X register source.");
        if (is_s_register(dest) && !is_w_register(src)) throw std::runtime_error("FMOV to S register requires W register source.");
        uint32_t op = is_d_register(dest) ? 0x9E670000 : 0x1E270000;
        emit(op | (to_reg(src) << 5) | to_reg(dest));
    } else if (!dest_is_fp && src_is_fp) { // FPR to GPR
        if (is_x_register(dest) && !is_d_register(src)) throw std::runtime_error("FMOV to X register requires D register source.");
        if (is_w_register(dest) && !is_s_register(src)) throw std::runtime_error("FMOV to W register requires S register source.");
        uint32_t op = is_x_register(dest) ? 0x9E660000 : 0x1E260000;
        emit(op | (to_reg(src) << 5) | to_reg(dest));
    } else {
        mov(dest, src);
    }
}

void AssemblerAArch64::fmov(Register dest, double imm) {
    if (is_d_register(dest)) {
        union { double f; uint64_t i; } u = {imm};
        mov(Register::X16, u.i);
        fmov(dest, Register::X16);
    } else if (is_s_register(dest)) {
        union { float f; uint32_t i; } u = {static_cast<float>(imm)};
        mov(Register::W16, u.i);
        fmov(dest, Register::W16);
    } else {
        throw std::runtime_error("fmov immediate requires a floating-point destination register");
    }
}

void AssemblerAArch64::fcmp(Register rn, Register rm) {
    uint32_t type = is_d_register(rn) ? 1 : 0;
    emit(0x1E202000 | (type << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5));
}

void AssemblerAArch64::fcmp(Register rn, double imm) {
    if (imm != 0.0) {
        throw std::runtime_error("fcmp immediate only supports 0.0");
    }
    uint32_t type = is_d_register(rn) ? 1 : 0;
    // FCMP (zero) instruction encoding
    emit(0x1E202008 | (type << 22) | (to_reg(rn) << 5));
}

void AssemblerAArch64::scvtf(Register rd, Register rn) {
    uint32_t sf = is_x_register(rn) ? 1 : 0;
    uint32_t type = is_d_register(rd) ? 1 : 0;
    emit(0x1E220000 | (sf << 31) | (type << 22) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::fcvtzs(Register rd, Register rn) {
    uint32_t sf = is_x_register(rd) ? 1 : 0;
    uint32_t type = is_d_register(rn) ? 1 : 0;
    emit(0x1E380000 | (sf << 31) | (type << 22) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::adr(Register rd, uintptr_t target_address) {
    int64_t offset = target_address - current_address_;
    if (offset < -1048576 || offset > 1048575) throw std::runtime_error("ADR offset out of range");
    uint32_t immlo = offset & 0x3;
    uint32_t immhi = (offset >> 2) & 0x7FFFF;
    emit(0x10000000 | (immlo << 29) | (immhi << 5) | to_reg(rd));
}

void AssemblerAArch64::adrp(Register rd, uintptr_t target_address) {
    int64_t offset = (target_address & ~0xFFF) - (current_address_ & ~0xFFF);
    if (offset < -2147483648 || offset > 2147483647) throw std::runtime_error("ADRP offset out of range");
    uint32_t immlo = (offset >> 12) & 0x3;
    uint32_t immhi = (offset >> 14) & 0x7FFFF;
    emit(0x90000000 | (immlo << 29) | (immhi << 5) | to_reg(rd));
}

void AssemblerAArch64::add(Register rd, Register rn, Register rm, uint32_t shift, uint32_t amount) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x0B000000 | (to_reg(rm) << 16) | (shift << 22) | (amount << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::sub(Register rd, Register rn, Register rm, uint32_t shift, uint32_t amount) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x4B000000 | (to_reg(rm) << 16) | (shift << 22) | (amount << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::ldr(Register rt, uintptr_t address) {
    gen_load_address(is_w_register(rt) ? Register::W16 : Register::X16, address);
    ldr(rt, is_w_register(rt) ? Register::W16 : Register::X16);
}

void AssemblerAArch64::str(Register rt, uintptr_t address) {
    gen_load_address(is_w_register(rt) ? Register::W16 : Register::X16, address);
    str(rt, is_w_register(rt) ? Register::W16 : Register::X16);
}

void AssemblerAArch64::ldr(Register rt, Register rn, int32_t offset) {
    uint32_t size = is_w_register(rt) ? 2 : 3; // 2 for 32-bit, 3 for 64-bit
    if (offset >= 0 && offset < (1 << 12) * (1 << size) && (offset % (1 << size) == 0)) {
        uint32_t imm12 = (offset >> size) & 0xFFF;
        emit((size << 30) | 0x39400000 | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
    } else {
        ldur(rt, rn, offset);
    }
}

void AssemblerAArch64::str(Register rt, Register rn, int32_t offset) {
    uint32_t size = is_w_register(rt) ? 2 : 3; // 2 for 32-bit, 3 for 64-bit
    if (offset >= 0 && offset < (1 << 12) * (1 << size) && (offset % (1 << size) == 0)) {
        uint32_t imm12 = (offset >> size) & 0xFFF;
        emit((size << 30) | 0x39000000 | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
    } else {
        stur(rt, rn, offset);
    }
}

void AssemblerAArch64::ldur(Register rt, Register rn, int32_t offset) {
    if (offset < -256 || offset > 255) throw std::runtime_error("LDUR offset out of range");
    uint32_t size = is_w_register(rt) ? 2 : 3;
    uint32_t imm9 = offset & 0x1FF;
    emit((size << 30) | 0x38400000 | (imm9 << 12) | (to_reg(rn) << 5) | to_reg(rt));
}

void AssemblerAArch64::stur(Register rt, Register rn, int32_t offset) {
    if (offset < -256 || offset > 255) throw std::runtime_error("STUR offset out of range");
    uint32_t size = is_w_register(rt) ? 2 : 3;
    uint32_t imm9 = offset & 0x1FF;
    emit((size << 30) | 0x38000000 | (imm9 << 12) | (to_reg(rn) << 5) | to_reg(rt));
}

void AssemblerAArch64::ldrh(Register rt, Register rn, int32_t offset) {
    if (!is_w_register(rt)) throw std::runtime_error("LDRH requires a W register target");
    // Unsigned immediate
    if (offset >= 0 && offset < 4096 * 2 && (offset % 2 == 0)) {
        uint32_t imm12 = (offset >> 1) & 0xFFF;
        emit(0x79400000 | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
    } else if (offset >= -256 && offset < 256) { // Unscaled immediate
        uint32_t imm9 = offset & 0x1FF;
        emit(0x78400000 | (imm9 << 12) | (to_reg(rn) << 5) | to_reg(rt));
    } else {
        throw std::runtime_error("LDRH offset out of range");
    }
}

void AssemblerAArch64::ldrb(Register rt, Register rn, int32_t offset) {
    if (!is_w_register(rt)) throw std::runtime_error("LDRB requires a W register target");
    // Unsigned immediate
    if (offset >= 0 && offset < 4096) {
        uint32_t imm12 = offset & 0xFFF;
        emit(0x39400000 | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
    } else if (offset >= -256 && offset < 256) { // Unscaled immediate
        uint32_t imm9 = offset & 0x1FF;
        emit(0x38400000 | (imm9 << 12) | (to_reg(rn) << 5) | to_reg(rt));
    } else {
        throw std::runtime_error("LDRB offset out of range");
    }
}

void AssemblerAArch64::ldrsw(Register rt, Register rn, int32_t offset) {
    if (!is_x_register(rt)) throw std::runtime_error("LDRSW requires an X register target");
    // Unsigned immediate
    if (offset >= 0 && offset < 4096 * 4 && (offset % 4 == 0)) {
        uint32_t imm12 = (offset >> 2) & 0xFFF;
        emit(0xB9800000 | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
    } else if (offset >= -256 && offset < 256) { // Unscaled immediate
        uint32_t imm9 = offset & 0x1FF;
        emit(0xB8800000 | (imm9 << 12) | (to_reg(rn) << 5) | to_reg(rt));
    } else {
        throw std::runtime_error("LDRSW offset out of range");
    }
}

void AssemblerAArch64::ldrsh(Register rt, Register rn, int32_t offset) {
    uint32_t op_scaled, op_unscaled;
    if (is_w_register(rt)) {
        op_scaled = 0x79C00000;
        op_unscaled = 0x78C00000;
    } else if (is_x_register(rt)) {
        op_scaled = 0x79800000;
        op_unscaled = 0x78800000;
    } else {
        throw std::runtime_error("LDRSH requires a W or X register target");
    }

    if (offset >= 0 && offset < 4096 * 2 && (offset % 2 == 0)) {
        uint32_t imm12 = (offset >> 1) & 0xFFF;
        emit(op_scaled | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
    } else if (offset >= -256 && offset < 256) {
        uint32_t imm9 = offset & 0x1FF;
        emit(op_unscaled | (imm9 << 12) | (to_reg(rn) << 5) | to_reg(rt));
    } else {
        throw std::runtime_error("LDRSH offset out of range");
    }
}

void AssemblerAArch64::ldrsb(Register rt, Register rn, int32_t offset) {
    uint32_t op_scaled, op_unscaled;
    if (is_w_register(rt)) {
        op_scaled = 0x39C00000;
        op_unscaled = 0x38C00000;
    } else if (is_x_register(rt)) {
        op_scaled = 0x39800000;
        op_unscaled = 0x38800000;
    } else {
        throw std::runtime_error("LDRSB requires a W or X register target");
    }

    if (offset >= 0 && offset < 4096) {
        uint32_t imm12 = offset & 0xFFF;
        emit(op_scaled | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
    } else if (offset >= -256 && offset < 256) {
        uint32_t imm9 = offset & 0x1FF;
        emit(op_unscaled | (imm9 << 12) | (to_reg(rn) << 5) | to_reg(rt));
    } else {
        throw std::runtime_error("LDRSB offset out of range");
    }
}

void AssemblerAArch64::strh(Register rt, Register rn, int32_t offset) {
    if (!is_w_register(rt)) throw std::runtime_error("STRH requires a W register source");
    // Unsigned immediate
    if (offset >= 0 && offset < 4096 * 2 && (offset % 2 == 0)) {
        uint32_t imm12 = (offset >> 1) & 0xFFF;
        emit(0x79000000 | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
    } else if (offset >= -256 && offset < 256) { // Unscaled immediate
        uint32_t imm9 = offset & 0x1FF;
        emit(0x78000000 | (imm9 << 12) | (to_reg(rn) << 5) | to_reg(rt));
    } else {
        throw std::runtime_error("STRH offset out of range");
    }
}

void AssemblerAArch64::strb(Register rt, Register rn, int32_t offset) {
    if (!is_w_register(rt)) throw std::runtime_error("STRB requires a W register source");
    // Unsigned immediate
    if (offset >= 0 && offset < 4096) {
        uint32_t imm12 = offset & 0xFFF;
        emit(0x39000000 | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
    } else if (offset >= -256 && offset < 256) { // Unscaled immediate
        uint32_t imm9 = offset & 0x1FF;
        emit(0x38000000 | (imm9 << 12) | (to_reg(rn) << 5) | to_reg(rt));
    } else {
        throw std::runtime_error("STRB offset out of range");
    }
}

void AssemblerAArch64::ldp(Register rt1, Register rt2, Register rn, int32_t offset, bool post_index) {
    uint32_t opc = is_w_register(rt1) ? 0 : 2;
    int scale = is_w_register(rt1) ? 2 : 3;
    if (offset < -256 * (1 << scale) || offset > 255 * (1 << scale) || offset % (1 << scale) != 0) throw std::runtime_error("LDP offset out of range");
    uint32_t imm7 = (offset >> scale) & 0x7F;
    uint32_t p_w_bits = post_index ? 0b01 : 0b10;
    emit((opc << 30) | 0x28400000 | (p_w_bits << 23) | (imm7 << 15) | (to_reg(rt2) << 10) | (to_reg(rn) << 5) | to_reg(rt1));
}

void AssemblerAArch64::stp(Register rt1, Register rt2, Register rn, int32_t offset, bool pre_index) {
    uint32_t opc = is_w_register(rt1) ? 0 : 2;
    int scale = is_w_register(rt1) ? 2 : 3;
    if (offset < -256 * (1 << scale) || offset > 255 * (1 << scale) || offset % (1 << scale) != 0) throw std::runtime_error("STP offset out of range");
    uint32_t imm7 = (offset >> scale) & 0x7F;
    uint32_t p_w_bits = pre_index ? 0b11 : 0b10;
    emit((opc << 30) | 0x28000000 | (p_w_bits << 23) | (imm7 << 15) | (to_reg(rt2) << 10) | (to_reg(rn) << 5) | to_reg(rt1));
}

void AssemblerAArch64::ldr_literal(Register rt, int64_t offset) {
    if (offset < -1048576 || offset > 1048575) throw std::runtime_error("LDR literal offset out of range");
    uint32_t opc = is_w_register(rt) ? 0 : 1;
    uint32_t imm19 = (offset >> 2) & 0x7FFFF;
    emit((opc << 30) | 0x18000000 | (imm19 << 5) | to_reg(rt));
}

void AssemblerAArch64::bfi(Register rd, Register rn, unsigned lsb, unsigned width) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    unsigned reg_size = sf ? 64 : 32;
    if (width == 0 || width > reg_size) throw std::runtime_error("BFI width out of range");
    if (lsb >= reg_size) throw std::runtime_error("BFI lsb out of range");
    uint32_t immr = (reg_size - lsb) % reg_size;
    uint32_t imms = width - 1;
    emit((sf << 31) | (sf << 22) | 0x13000000 | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::sbfx(Register rd, Register rn, unsigned lsb, unsigned width) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    unsigned reg_size = sf ? 64 : 32;
    if (width == 0 || width > reg_size) throw std::runtime_error("SBFX width out of range");
    if (lsb >= reg_size) throw std::runtime_error("SBFX lsb out of range");
    uint32_t immr = lsb;
    uint32_t imms = lsb + width - 1;
    emit((sf << 31) | (sf << 22) | 0x13000000 | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::ubfx(Register rd, Register rn, unsigned lsb, unsigned width) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    unsigned reg_size = sf ? 64 : 32;
    if (width == 0 || width > reg_size) throw std::runtime_error("UBFX width out of range");
    if (lsb >= reg_size) throw std::runtime_error("UBFX lsb out of range");
    uint32_t immr = lsb;
    uint32_t imms = lsb + width - 1;
    emit((sf << 31) | (sf << 22) | 0x53000000 | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void AssemblerAArch64::nop() {
    emit(0xD503201F);
}

void AssemblerAArch64::svc(uint16_t imm) {
    emit(0xD4000001 | (static_cast<uint32_t>(imm) << 5));
}

void AssemblerAArch64::mrs(Register rt, SystemRegister sys_reg) {
    emit(0xD5300000 | to_sys_reg(sys_reg) | to_reg(rt));
}

void AssemblerAArch64::msr(SystemRegister sys_reg, Register rt) {
    emit(0xD5100000 | to_sys_reg(sys_reg) | to_reg(rt));
}

void AssemblerAArch64::dmb(BarrierOption option) {
    uint32_t imm4;
    switch (option) {
        case BarrierOption::OSH: imm4 = 0b0011; break;
        case BarrierOption::NSH: imm4 = 0b0111; break;
        case BarrierOption::ISH: imm4 = 0b1011; break;
        case BarrierOption::SY:  imm4 = 0b1111; break;
        default: throw std::runtime_error("Invalid barrier option");
    }
    emit(0xD50330BF | (imm4 << 8));
}

void AssemblerAArch64::dsb(BarrierOption option) {
    uint32_t imm4;
    switch (option) {
        case BarrierOption::OSH: imm4 = 0b1010; break;
        case BarrierOption::NSH: imm4 = 0b0110; break;
        case BarrierOption::ISH: imm4 = 0b1011; break;
        case BarrierOption::SY:  imm4 = 0b1110; break;
        default: throw std::runtime_error("Invalid barrier option");
    }
    emit(0xD503309F | (imm4 << 8));
}

void AssemblerAArch64::isb() {
    emit(0xD5033FDF); // ISB SY
}

void AssemblerAArch64::ldxr(Register rt, Register rn) {
    uint32_t size = is_w_register(rt) ? 2 : 3;
    emit((size << 30) | 0x085F7C00 | (to_reg(rn) << 5) | to_reg(rt));
}

void AssemblerAArch64::stxr(Register rs, Register rt, Register rn) {
    uint32_t size = is_w_register(rt) ? 2 : 3;
    emit(
        (size << 30) |
        0x08007C00 |                // ← STXR 正确 opcode base
        (to_reg(rs) << 16) |
        (to_reg(rn) << 5) |
        to_reg(rt)
    );
}
void AssemblerAArch64::ldaxr(Register rt, Register rn) {
    uint32_t size = is_w_register(rt) ? 2 : 3;
    emit((size << 30) | 0x085F7C00 | (1 << 15) | (to_reg(rn) << 5) | to_reg(rt));
}

void AssemblerAArch64::stlxr(Register rs, Register rt, Register rn) {
    // rs = status/result reg; rt = data reg; rn = address reg
    uint32_t size = is_w_register(rt) ? 2 : 3;         // W → 2, X → 3
    constexpr uint32_t STLXR_BASE = 0x0800FC00u;       // bit15 + bit23 set
    uint32_t instr = 
          (size << 30)    // [31:30]
        | STLXR_BASE      // [29:0] 中的 opcode + fixed bits
        | (to_reg(rs) << 16) 
        | (to_reg(rn) << 5)
        | to_reg(rt);
    emit(instr);
}
void AssemblerAArch64::ldar(Register rt, Register rn) {
    uint32_t size = is_w_register(rt) ? 2 : 3;
    emit((size << 30) | 0x08DF7C00 | (to_reg(rn) << 5) | to_reg(rt));
}

void AssemblerAArch64::stlr(Register rt, Register rn) {
    uint32_t size = is_w_register(rt) ? 2 : 3;
    emit((size << 30) | 0x089F7C00 | (to_reg(rn) << 5) | to_reg(rt));
}

void AssemblerAArch64::call_function(uintptr_t destination) {
    gen_abs_call(destination, Register::X17); // Use X17 as a scratch register
}

void AssemblerAArch64::push(Register reg) {
    if (is_w_register(reg)) {
        stp(reg, Register::WZR, Register::SP, -16, true);
    } else {
        stp(reg, Register::ZR, Register::SP, -16, true);
    }
}

void AssemblerAArch64::pop(Register reg) {
    if (is_w_register(reg)) {
        ldp(reg, Register::WZR, Register::SP, 16, true);
    } else {
        ldp(reg, Register::ZR, Register::SP, 16, true);
    }
}

void AssemblerAArch64::load_constant(Register dest, uint64_t value) {
    mov(dest, value);
}

const std::vector<uint32_t>& AssemblerAArch64::get_code() const {
    return code_;
}

std::vector<uint32_t>& AssemblerAArch64::get_code_mut() {
    return code_;
}

size_t AssemblerAArch64::get_code_size() const {
    return code_.size() * 4;
}

uintptr_t AssemblerAArch64::get_current_address() const {
    return current_address_;
}

// Helper to encode a bitmask into N, imms, and immr fields
bool AssemblerAArch64::try_encode_logical_imm(uint64_t imm, uint32_t& out_N, uint32_t& out_imms, uint32_t& out_immr) {
    if (imm == 0 || imm == ~0ULL) return false;

    auto count_trailing_zeros = [](uint64_t n) {
        if (n == 0) return 64;
        int count = 0;
        while ((n & 1) == 0) {
            n >>= 1;
            count++;
        }
        return count;
    };

    auto count_leading_zeros = [](uint64_t n) {
        if (n == 0) return 64;
        int count = 0;
        uint64_t mask = 1ULL << 63;
        while ((n & mask) == 0) {
            n <<= 1;
            count++;
        }
        return count;
    };

    int size = 64;
    do {
        int trailing_zeros = count_trailing_zeros(imm);
        int leading_zeros = count_leading_zeros(imm);
        int set_bits = size - trailing_zeros - leading_zeros;

        if (set_bits > 0) {
            uint64_t mask = (1ULL << set_bits) - 1;
            if ((imm >> trailing_zeros) == mask) {
                out_N = (size == 64);
                out_immr = (size - trailing_zeros) % size;
                out_imms = set_bits - 1;
                return true;
            }
        }
        size /= 2;
        imm &= (1ULL << size) - 1;
    } while (size >= 2);

    return false;
}

} // namespace ur::assembler::arm64
