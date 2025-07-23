#include "ur/assembler.h"
#include <stdexcept>

namespace ur::assembler {

namespace {
    bool is_s_register(Register reg) {
        return reg >= Register::S0 && reg <= Register::S31;
    }
    bool is_d_register(Register reg) {
        return reg >= Register::D0 && reg <= Register::D31;
    }
    bool is_q_register(Register reg) {
        return reg >= Register::Q0 && reg <= Register::Q31;
    }

    uint32_t to_neon_arrangement(NeonArrangement arr, uint32_t& Q) {
        Q = 0;
        switch (arr) {
            case NeonArrangement::B8:  return 0x00;
            case NeonArrangement::B16: Q = 1; return 0x00;
            case NeonArrangement::H4:  return 0x01;
            case NeonArrangement::H8:  Q = 1; return 0x01;
            case NeonArrangement::S2:  return 0x02;
            case NeonArrangement::S4:  Q = 1; return 0x02;
            case NeonArrangement::D1:  return 0x03;
            case NeonArrangement::D2:  Q = 1; return 0x03;
            default: throw std::runtime_error("Invalid NEON arrangement");
        }
    }
}

void Assembler::neon_add(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t size = to_neon_arrangement(arr, Q);
    emit(0x0E208400 | (Q << 30) | (size << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}



void Assembler::neon_mul(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t size = to_neon_arrangement(arr, Q);
    emit(0x0E209C00 | (Q << 30) | (size << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::neon_and(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t size = to_neon_arrangement(arr, Q);
    emit(0x0E200400 | (Q << 30) | (size << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::neon_orr(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t size = to_neon_arrangement(arr, Q);
    emit(0x0E200C00 | (Q << 30) | (size << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::neon_eor(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t size = to_neon_arrangement(arr, Q);
    emit(0x2E200400 | (Q << 30) | (size << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::neon_cmeq(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t size = to_neon_arrangement(arr, Q);
    emit(0x2E208C00 | (Q << 30) | (size << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::neon_cmgt(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t size = to_neon_arrangement(arr, Q);
    emit(0x0E203400 | (Q << 30) | (size << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::neon_cmge(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t size = to_neon_arrangement(arr, Q);
    emit(0x0E203C00 | (Q << 30) | (size << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

namespace {
    uint32_t to_fp_neon_arrangement(NeonArrangement arr, uint32_t& Q) {
        uint32_t sz;
        switch (arr) {
            case NeonArrangement::S2:  sz = 0; Q = 0; break;
            case NeonArrangement::S4:  sz = 0; Q = 1; break;
            case NeonArrangement::D2:  sz = 1; Q = 1; break;
            default: throw std::runtime_error("Invalid NEON floating-point arrangement. Must be S2, S4, or D2.");
        }
        return sz;
    }
}

void Assembler::neon_fadd(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t sz = to_fp_neon_arrangement(arr, Q);
    emit(0x0E20D400 | (Q << 30) | (sz << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}




void Assembler::neon_fdiv(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t sz = to_fp_neon_arrangement(arr, Q);
    emit(0x2E20FC00 | (Q << 30) | (sz << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::neon_fcmeq(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t sz = to_fp_neon_arrangement(arr, Q);
    emit(0x0E20E400 | (Q << 30) | (sz << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}



void Assembler::neon_str(Register rt, Register rn, int32_t offset) {
    uint32_t size = 0, opc = 0;
    uint32_t scale = 0;
    if (is_s_register(rt))      { size = 0b10; opc = 0b00; scale = 2; }
    else if (is_d_register(rt)) { size = 0b11; opc = 0b00; scale = 3; }
    else if (is_q_register(rt)) { size = 0b00; opc = 0b01; scale = 4; }
    else { throw std::runtime_error("Unsupported register for NEON STR"); }

    if (offset != 0 && (offset % (1 << scale) != 0)) {
        throw std::runtime_error("NEON STR offset must be a multiple of the element size");
    }
    uint32_t imm12 = offset == 0 ? 0 : (offset / (1 << scale)) & 0xFFF;
    emit((size << 30) | (opc << 22) | 0x3C000000 | (1 << 24) | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
}

} // namespace ur::assembler
