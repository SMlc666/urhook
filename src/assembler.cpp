#include "ur/assembler.h"
#include <stdexcept>
#include <cmath>

namespace ur::assembler {

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
}

Assembler::Assembler(uintptr_t start_address) : current_address_(start_address) {}

void Assembler::emit(uint32_t instruction) {
    code_.push_back(instruction);
    current_address_ += 4;
}

uint32_t Assembler::to_reg(Register reg) {
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

uint32_t Assembler::to_cond(Condition cond) {
    return static_cast<uint32_t>(cond);
}

void Assembler::gen_abs_jump(uintptr_t destination, Register reg) {
    movz(reg, destination & 0xFFFF, 0);
    movk(reg, (destination >> 16) & 0xFFFF, 16);
    movk(reg, (destination >> 32) & 0xFFFF, 32);
    movk(reg, (destination >> 48) & 0xFFFF, 48);
    br(reg);
}

void Assembler::gen_abs_call(uintptr_t destination, Register reg) {
    stp(Register::FP, Register::LR, Register::SP, -16, true);
    movz(reg, destination & 0xFFFF, 0);
    movk(reg, (destination >> 16) & 0xFFFF, 16);
    movk(reg, (destination >> 32) & 0xFFFF, 32);
    movk(reg, (destination >> 48) & 0xFFFF, 48);
    blr(reg);
    ldp(Register::FP, Register::LR, Register::SP, 16, true);
}

void Assembler::gen_load_address(Register dest, uintptr_t address) {
    mov(dest, address);
}

void Assembler::b(uintptr_t target_address) {
    int64_t offset = target_address - current_address_;
    if (offset < -134217728 || offset > 134217724) throw std::runtime_error("Branch offset out of range");
    uint32_t imm26 = (static_cast<uint32_t>(offset) >> 2) & 0x3FFFFFF;
    emit(0x14000000 | imm26);
}

void Assembler::b(Condition cond, uintptr_t target_address) {
    int64_t offset = target_address - current_address_;
    if (offset < -524288 || offset > 524284) throw std::runtime_error("Conditional branch offset out of range");
    uint32_t imm19 = (static_cast<uint32_t>(offset) >> 2) & 0x7FFFF;
    emit(0x54000000 | (imm19 << 5) | to_cond(cond));
}

void Assembler::bl(uintptr_t target_address) {
    int64_t offset = target_address - current_address_;
    if (offset < -134217728 || offset > 134217724) throw std::runtime_error("Branch with link offset out of range");
    uint32_t imm26 = (static_cast<uint32_t>(offset) >> 2) & 0x3FFFFFF;
    emit(0x94000000 | imm26);
}

void Assembler::blr(Register reg) {
    emit(0xD63F0000 | (to_reg(reg) << 5));
}

void Assembler::br(Register reg) {
    emit(0xD61F0000 | (to_reg(reg) << 5));
}

void Assembler::ret() {
    emit(0xD65F03C0); // RET is an alias for BR LR
}

void Assembler::cbz(Register rt, uintptr_t target_address) {
    int64_t offset = target_address - current_address_;
    if (offset < 0 || offset > 524284) throw std::runtime_error("CBZ offset out of range");
    uint32_t imm19 = (static_cast<uint32_t>(offset) >> 2) & 0x7FFFF;
    emit(0xB4000000 | (imm19 << 5) | to_reg(rt));
}

void Assembler::cbnz(Register rt, uintptr_t target_address) {
    int64_t offset = target_address - current_address_;
    if (offset < 0 || offset > 524284) throw std::runtime_error("CBNZ offset out of range");
    uint32_t imm19 = (static_cast<uint32_t>(offset) >> 2) & 0x7FFFF;
    emit(0xB5000000 | (imm19 << 5) | to_reg(rt));
}

void Assembler::add(Register rd, Register rn, uint16_t imm, bool shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    uint32_t sh = shift ? 1 : 0;
    emit((sf << 31) | 0x11000000 | (sh << 22) | (static_cast<uint32_t>(imm) << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::sub(Register rd, Register rn, uint16_t imm, bool shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    uint32_t sh = shift ? 1 : 0;
    emit((sf << 31) | 0x51000000 | (sh << 22) | (static_cast<uint32_t>(imm) << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::and_(Register rd, Register rn, uint64_t bitmask) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    uint32_t n, imms, immr;
    if (!try_encode_logical_imm(bitmask, n, imms, immr)) throw std::runtime_error("Invalid bitmask for AND");
    if (sf == 0 && n != 0) throw std::runtime_error("Invalid bitmask for 32-bit AND");
    emit((sf << 31) | 0x12000000 | (n << 22) | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::orr(Register rd, Register rn, uint64_t bitmask) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    uint32_t n, imms, immr;
    if (!try_encode_logical_imm(bitmask, n, imms, immr)) throw std::runtime_error("Invalid bitmask for ORR");
    if (sf == 0 && n != 0) throw std::runtime_error("Invalid bitmask for 32-bit ORR");
    emit((sf << 31) | 0x32000000 | (n << 22) | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::eor(Register rd, Register rn, uint64_t bitmask) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    uint32_t n, imms, immr;
    if (!try_encode_logical_imm(bitmask, n, imms, immr)) throw std::runtime_error("Invalid bitmask for EOR");
    if (sf == 0 && n != 0) throw std::runtime_error("Invalid bitmask for 32-bit EOR");
    emit((sf << 31) | 0x52000000 | (n << 22) | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::mov(Register rd, uint64_t imm) {
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

void Assembler::mov(Register rd, Register rn) {
    if (rn == Register::SP || rn == Register::WSP) {
        // MOV (register) from SP is an alias for ADD (immediate) with imm=0.
        add(rd, rn, 0);
    } else {
        // For other GPRs, MOV is an alias for ORR with the zero register.
        orr(rd, is_w_register(rd) ? Register::WZR : Register::ZR, rn);
    }
}

void Assembler::movn(Register rd, uint16_t imm, int shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x12800000 | ((shift / 16) << 21) | (uint32_t)imm << 5 | to_reg(rd));
}

void Assembler::movz(Register rd, uint16_t imm, int shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x52800000 | ((shift / 16) << 21) | (uint32_t)imm << 5 | to_reg(rd));
}

void Assembler::movk(Register rd, uint16_t imm, int shift) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x72800000 | ((shift / 16) << 21) | (uint32_t)imm << 5 | to_reg(rd));
}

void Assembler::add(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x0B000000 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::sub(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x4B000000 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::and_(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x0A000000 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::orr(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x2A000000 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::eor(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x4A000000 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::cmp(Register rn, Register rm) {
    sub(is_w_register(rn) ? Register::WZR : Register::ZR, rn, rm);
}

void Assembler::cset(Register rd, Condition cond) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    Condition inverted_cond = static_cast<Condition>(static_cast<uint32_t>(cond) ^ 1);
    emit((sf << 31) | 0x1A800400 | to_reg(rd) | (to_cond(inverted_cond) << 12) | (to_reg(is_w_register(rd) ? Register::WZR : Register::ZR) << 5) | (to_reg(is_w_register(rd) ? Register::WZR : Register::ZR) << 16));
}

void Assembler::mul(Register rd, Register rn, Register rm) {
    madd(rd, rn, rm, is_w_register(rd) ? Register::WZR : Register::ZR);
}

void Assembler::sdiv(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x1AC00C00 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::udiv(Register rd, Register rn, Register rm) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x1AC00800 | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::madd(Register rd, Register rn, Register rm, Register ra) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x1B000000 | (to_reg(rm) << 16) | (to_reg(ra) << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::msub(Register rd, Register rn, Register rm, Register ra) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x1B008000 | (to_reg(rm) << 16) | (to_reg(ra) << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::fadd(Register rd, Register rn, Register rm) {
    uint32_t type = is_d_register(rd) ? 1 : 0;
    emit(0x1E202800 | (type << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::fsub(Register rd, Register rn, Register rm) {
    uint32_t type = is_d_register(rd) ? 1 : 0;
    emit(0x1E203800 | (type << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::fmul(Register rd, Register rn, Register rm) {
    uint32_t type = is_d_register(rd) ? 1 : 0;
    emit(0x1E200800 | (type << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::fdiv(Register rd, Register rn, Register rm) {
    uint32_t type = is_d_register(rd) ? 1 : 0;
    emit(0x1E201800 | (type << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::fmov(Register dest, Register src) {
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

void Assembler::fmov(Register dest, double imm) {
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

void Assembler::fcmp(Register rn, Register rm) {
    uint32_t type = is_d_register(rn) ? 1 : 0;
    emit(0x1E202000 | (type << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5));
}

void Assembler::fcmp(Register rn, double imm) {
    if (imm != 0.0) {
        throw std::runtime_error("fcmp immediate only supports 0.0");
    }
    uint32_t type = is_d_register(rn) ? 1 : 0;
    // FCMP (zero) instruction encoding
    emit(0x1E202008 | (type << 22) | (to_reg(rn) << 5));
}

void Assembler::scvtf(Register rd, Register rn) {
    uint32_t sf = is_x_register(rn) ? 1 : 0;
    uint32_t type = is_d_register(rd) ? 1 : 0;
    emit(0x1E220000 | (sf << 31) | (type << 22) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::fcvtzs(Register rd, Register rn) {
    uint32_t sf = is_x_register(rd) ? 1 : 0;
    uint32_t type = is_d_register(rn) ? 1 : 0;
    emit(0x1E380000 | (sf << 31) | (type << 22) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::adr(Register rd, uintptr_t target_address) {
    int64_t offset = target_address - current_address_;
    if (offset < -1048576 || offset > 1048575) throw std::runtime_error("ADR offset out of range");
    uint32_t immlo = offset & 0x3;
    uint32_t immhi = (offset >> 2) & 0x7FFFF;
    emit(0x10000000 | (immlo << 29) | (immhi << 5) | to_reg(rd));
}

void Assembler::adrp(Register rd, uintptr_t target_address) {
    int64_t offset = (target_address & ~0xFFF) - (current_address_ & ~0xFFF);
    if (offset < -2147483648 || offset > 2147483647) throw std::runtime_error("ADRP offset out of range");
    uint32_t immlo = (offset >> 12) & 0x3;
    uint32_t immhi = (offset >> 14) & 0x7FFFF;
    emit(0x90000000 | (immlo << 29) | (immhi << 5) | to_reg(rd));
}

void Assembler::add(Register rd, Register rn, Register rm, uint32_t shift, uint32_t amount) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x0B000000 | (to_reg(rm) << 16) | (shift << 22) | (amount << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::sub(Register rd, Register rn, Register rm, uint32_t shift, uint32_t amount) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    emit((sf << 31) | 0x4B000000 | (to_reg(rm) << 16) | (shift << 22) | (amount << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::ldr(Register rt, uintptr_t address) {
    gen_load_address(is_w_register(rt) ? Register::W16 : Register::X16, address);
    ldr(rt, is_w_register(rt) ? Register::W16 : Register::X16);
}

void Assembler::str(Register rt, uintptr_t address) {
    gen_load_address(is_w_register(rt) ? Register::W16 : Register::X16, address);
    str(rt, is_w_register(rt) ? Register::W16 : Register::X16);
}

void Assembler::ldr(Register rt, Register rn, int32_t offset) {
    uint32_t size = is_w_register(rt) ? 2 : 3; // 2 for 32-bit, 3 for 64-bit
    if (offset >= 0 && offset < (1 << 12) * (1 << size) && (offset % (1 << size) == 0)) {
        uint32_t imm12 = (offset >> size) & 0xFFF;
        emit((size << 30) | 0x39400000 | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
    } else {
        ldur(rt, rn, offset);
    }
}

void Assembler::str(Register rt, Register rn, int32_t offset) {
    uint32_t size = is_w_register(rt) ? 2 : 3; // 2 for 32-bit, 3 for 64-bit
    if (offset >= 0 && offset < (1 << 12) * (1 << size) && (offset % (1 << size) == 0)) {
        uint32_t imm12 = (offset >> size) & 0xFFF;
        emit((size << 30) | 0x39000000 | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
    } else {
        stur(rt, rn, offset);
    }
}

void Assembler::ldur(Register rt, Register rn, int32_t offset) {
    if (offset < -256 || offset > 255) throw std::runtime_error("LDUR offset out of range");
    uint32_t size = is_w_register(rt) ? 2 : 3;
    uint32_t imm9 = offset & 0x1FF;
    emit((size << 30) | 0x38400000 | (imm9 << 12) | (to_reg(rn) << 5) | to_reg(rt));
}

void Assembler::stur(Register rt, Register rn, int32_t offset) {
    if (offset < -256 || offset > 255) throw std::runtime_error("STUR offset out of range");
    uint32_t size = is_w_register(rt) ? 2 : 3;
    uint32_t imm9 = offset & 0x1FF;
    emit((size << 30) | 0x38000000 | (imm9 << 12) | (to_reg(rn) << 5) | to_reg(rt));
}

void Assembler::ldp(Register rt1, Register rt2, Register rn, int32_t offset, bool post_index) {
    uint32_t opc = is_w_register(rt1) ? 0 : 2;
    int scale = is_w_register(rt1) ? 2 : 3;
    if (offset < -256 * (1 << scale) || offset > 255 * (1 << scale) || offset % (1 << scale) != 0) throw std::runtime_error("LDP offset out of range");
    uint32_t imm7 = (offset >> scale) & 0x7F;
    uint32_t p_w_bits = post_index ? 0b01 : 0b10;
    emit((opc << 30) | 0x28400000 | (p_w_bits << 23) | (imm7 << 15) | (to_reg(rt2) << 10) | (to_reg(rn) << 5) | to_reg(rt1));
}

void Assembler::stp(Register rt1, Register rt2, Register rn, int32_t offset, bool pre_index) {
    uint32_t opc = is_w_register(rt1) ? 0 : 2;
    int scale = is_w_register(rt1) ? 2 : 3;
    if (offset < -256 * (1 << scale) || offset > 255 * (1 << scale) || offset % (1 << scale) != 0) throw std::runtime_error("STP offset out of range");
    uint32_t imm7 = (offset >> scale) & 0x7F;
    uint32_t p_w_bits = pre_index ? 0b11 : 0b10;
    emit((opc << 30) | 0x28000000 | (p_w_bits << 23) | (imm7 << 15) | (to_reg(rt2) << 10) | (to_reg(rn) << 5) | to_reg(rt1));
}

void Assembler::ldr_literal(Register rt, int64_t offset) {
    if (offset < -1048576 || offset > 1048575) throw std::runtime_error("LDR literal offset out of range");
    uint32_t opc = is_w_register(rt) ? 0 : 1;
    uint32_t imm19 = (offset >> 2) & 0x7FFFF;
    emit((opc << 30) | 0x18000000 | (imm19 << 5) | to_reg(rt));
}

void Assembler::bfi(Register rd, Register rn, unsigned lsb, unsigned width) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    unsigned reg_size = sf ? 64 : 32;
    if (width == 0 || width > reg_size) throw std::runtime_error("BFI width out of range");
    if (lsb >= reg_size) throw std::runtime_error("BFI lsb out of range");
    uint32_t immr = (reg_size - lsb) % reg_size;
    uint32_t imms = width - 1;
    emit((sf << 31) | (sf << 22) | 0x13000000 | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::sbfx(Register rd, Register rn, unsigned lsb, unsigned width) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    unsigned reg_size = sf ? 64 : 32;
    if (width == 0 || width > reg_size) throw std::runtime_error("SBFX width out of range");
    if (lsb >= reg_size) throw std::runtime_error("SBFX lsb out of range");
    uint32_t immr = lsb;
    uint32_t imms = lsb + width - 1;
    emit((sf << 31) | (sf << 22) | 0x13000000 | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::ubfx(Register rd, Register rn, unsigned lsb, unsigned width) {
    uint32_t sf = is_w_register(rd) ? 0 : 1;
    unsigned reg_size = sf ? 64 : 32;
    if (width == 0 || width > reg_size) throw std::runtime_error("UBFX width out of range");
    if (lsb >= reg_size) throw std::runtime_error("UBFX lsb out of range");
    uint32_t immr = lsb;
    uint32_t imms = lsb + width - 1;
    emit((sf << 31) | (sf << 22) | 0x53000000 | (immr << 16) | (imms << 10) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::nop() {
    emit(0xD503201F);
}

void Assembler::svc(uint16_t imm) {
    emit(0xD4000001 | (static_cast<uint32_t>(imm) << 5));
}

void Assembler::call_function(uintptr_t destination) {
    gen_abs_call(destination, Register::X17); // Use X17 as a scratch register
}

void Assembler::push(Register reg) {
    if (is_w_register(reg)) {
        stp(reg, Register::WZR, Register::SP, -16, true);
    } else {
        stp(reg, Register::ZR, Register::SP, -16, true);
    }
}

void Assembler::pop(Register reg) {
    if (is_w_register(reg)) {
        ldp(reg, Register::WZR, Register::SP, 16, true);
    } else {
        ldp(reg, Register::ZR, Register::SP, 16, true);
    }
}

void Assembler::load_constant(Register dest, uint64_t value) {
    mov(dest, value);
}

const std::vector<uint32_t>& Assembler::get_code() const {
    return code_;
}

std::vector<uint32_t>& Assembler::get_code_mut() {
    return code_;
}

size_t Assembler::get_code_size() const {
    return code_.size() * 4;
}

uintptr_t Assembler::get_current_address() const {
    return current_address_;
}

// Helper to encode a bitmask into N, imms, and immr fields
bool Assembler::try_encode_logical_imm(uint64_t imm, uint32_t& out_N, uint32_t& out_imms, uint32_t& out_immr) {
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

} // namespace ur::assembler