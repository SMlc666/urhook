#include "ur/assembler.h"
#include <stdexcept>

namespace ur::assembler {

namespace {
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

void Assembler::neon_sub(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t size = to_neon_arrangement(arr, Q);
    emit(0x2E208400 | (Q << 30) | (size << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::neon_mul(Register rd, Register rn, Register rm, NeonArrangement arr) {
    uint32_t Q;
    uint32_t size = to_neon_arrangement(arr, Q);
    emit(0x0E209C00 | (Q << 30) | (size << 22) | (to_reg(rm) << 16) | (to_reg(rn) << 5) | to_reg(rd));
}

void Assembler::neon_ldr(Register rt, Register rn, int32_t offset) {
    uint32_t size_field, opc_field, scale;

    if (is_d_register(rt)) {
        size_field = 0b11; // For D registers (64-bit), size is 11.
        opc_field = 0b01;  // Standard LDR opc.
        scale = 3;         // 2^3 = 8 bytes.
    } else if (is_q_register(rt)) {
        size_field = 0b00; // For Q registers (128-bit), size is 00.
        opc_field = 0b11;  // LDR (vector) has a special opc.
        scale = 4;         // 2^4 = 16 bytes.
    } else {
        throw std::runtime_error("Unsupported register for NEON LDR. Only D and Q registers are supported.");
    }

    if (offset < 0 || (offset % (1 << scale)) != 0) {
        throw std::runtime_error("NEON LDR offset must be positive and aligned to the register size.");
    }
    uint32_t imm12 = offset >> scale;
    if (imm12 > 0xFFF) {
        throw std::runtime_error("NEON LDR offset is out of range.");
    }

    // Encoding from ARMv8 manual: size[1:0] 111101 opc[1:0] imm12[11:0] Rn[4:0] Rt[4:0]
    // The fixed part 0b00111101... corresponds to 0x3D...
    emit((size_field << 30) | 0x3D000000 | (opc_field << 22) | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
}

void Assembler::neon_str(Register rt, Register rn, int32_t offset) {
    uint32_t size_field, opc_field, scale;

    if (is_d_register(rt)) {
        size_field = 0b11; // For D registers (64-bit), size is 11.
        opc_field = 0b00;  // Standard STR opc.
        scale = 3;         // 8 bytes.
    } else if (is_q_register(rt)) {
        size_field = 0b00; // For Q registers (128-bit), size is 00.
        opc_field = 0b10;  // STR (vector) has a special opc.
        scale = 4;         // 16 bytes.
    } else {
        throw std::runtime_error("Unsupported register for NEON STR. Only D and Q registers are supported.");
    }

    if (offset < 0 || (offset % (1 << scale)) != 0) {
        throw std::runtime_error("NEON STR offset must be positive and aligned to the register size.");
    }
    uint32_t imm12 = offset >> scale;
    if (imm12 > 0xFFF) {
        throw std::runtime_error("NEON STR offset is out of range.");
    }

    // Encoding from ARMv8 manual: size[1:0] 111101 opc[1:0] imm12[11:0] Rn[4:0] Rt[4:0]
    emit((size_field << 30) | 0x3D000000 | (opc_field << 22) | (imm12 << 10) | (to_reg(rn) << 5) | to_reg(rt));
}

} // namespace ur::assembler
