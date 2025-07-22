#pragma once

#include <vector>
#include <cstdint>

namespace ur::assembler {

// A64 registers
enum class Register {
    X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15,
    X16, X17, X18, X19, X20, X21, X22, X23, X24, X25, X26, X27, X28,
    FP = 29, // Frame Pointer
    LR = 30, // Link Register
    SP = 31, // Stack Pointer
    ZR = 32, // Zero Register (encoded as 31 in most cases)

    // W registers (32-bit)
    W0 = 64, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15,
    W16, W17, W18, W19, W20, W21, W22, W23, W24, W25, W26, W27, W28,
    WFP = W0 + 29,
    WLR = W0 + 30,
    WSP = W0 + 31,
    WZR = W0 + 32,

    // Floating-point / SIMD registers
    S0 = 100, S1, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, S12, S13, S14, S15,
    S16, S17, S18, S19, S20, S21, S22, S23, S24, S25, S26, S27, S28, S29, S30, S31,

    D0 = 150, D1, D2, D3, D4, D5, D6, D7, D8, D9, D10, D11, D12, D13, D14, D15,
    D16, D17, D18, D19, D20, D21, D22, D23, D24, D25, D26, D27, D28, D29, D30, D31,

    // NEON / SIMD vector registers
    Q0 = 200, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, Q10, Q11, Q12, Q13, Q14, Q15,
    Q16, Q17, Q18, Q19, Q20, Q21, Q22, Q23, Q24, Q25, Q26, Q27, Q28, Q29, Q30, Q31,
};

enum class Condition {
    EQ = 0x0, NE = 0x1, CS = 0x2, HS = 0x2, CC = 0x3, LO = 0x3,
    MI = 0x4, PL = 0x5, VS = 0x6, VC = 0x7, HI = 0x8, LS = 0x9,
    GE = 0xa, LT = 0xb, GT = 0xc, LE = 0xd, AL = 0xe, NV = 0xf
};

enum class NeonArrangement {
    B8, B16,
    H4, H8,
    S2, S4,
    D1, D2
};

class Assembler {
public:
    explicit Assembler(uintptr_t start_address);
    virtual ~Assembler() = default;

    Assembler(const Assembler&) = delete;
    Assembler& operator=(const Assembler&) = delete;
    Assembler(Assembler&&) = default;
    Assembler& operator=(Assembler&&) = default;

    // Pseudo-instructions for absolute jumps and calls
    void gen_abs_jump(uintptr_t destination, Register reg);
    void gen_abs_call(uintptr_t destination, Register reg);
    void gen_load_address(Register dest, uintptr_t address);

    // Branch instructions
    void b(uintptr_t target_address);
    void b(Condition cond, uintptr_t target_address);
    void bl(uintptr_t target_address);
    void blr(Register reg);
    void br(Register reg);
    void ret();

    // Compare and branch instructions
    void cbz(Register rt, uintptr_t target_address);
    void cbnz(Register rt, uintptr_t target_address);

    // Data processing - immediate
    void add(Register rd, Register rn, uint16_t imm, bool shift = false);
    void sub(Register rd, Register rn, uint16_t imm, bool shift = false);
    void and_(Register rd, Register rn, uint64_t bitmask);
    void orr(Register rd, Register rn, uint64_t bitmask);
    void eor(Register rd, Register rn, uint64_t bitmask);
    void mov(Register rd, uint64_t imm); // Assembles to MOVZ/MOVK
    void mov(Register rd, Register rn); // Assembles to ORR
    void movn(Register rd, uint16_t imm, int shift = 0);
    void movz(Register rd, uint16_t imm, int shift = 0);
    void movk(Register rd, uint16_t imm, int shift = 0);

    // Data processing - register
    void add(Register rd, Register rn, Register rm);
    void sub(Register rd, Register rn, Register rm);
    void add(Register rd, Register rn, Register rm, uint32_t shift, uint32_t amount);
    void sub(Register rd, Register rn, Register rm, uint32_t shift, uint32_t amount);
    void and_(Register rd, Register rn, Register rm);
    void orr(Register rd, Register rn, Register rm);
    void eor(Register rd, Register rn, Register rm);
    void cmp(Register rn, Register rm);
    void cset(Register rd, Condition cond);

    // Multiply and divide
    void mul(Register rd, Register rn, Register rm);
    void sdiv(Register rd, Register rn, Register rm);
    void udiv(Register rd, Register rn, Register rm);
    void madd(Register rd, Register rn, Register rm, Register ra);
    void msub(Register rd, Register rn, Register rm, Register ra);

    // Floating-point data processing
    void fadd(Register rd, Register rn, Register rm);
    void fsub(Register rd, Register rn, Register rm);
    void fmul(Register rd, Register rn, Register rm);
    void fdiv(Register rd, Register rn, Register rm);
    void fmov(Register dest, Register src);
    void fmov(Register dest, double imm);
    void fcmp(Register rn, Register rm);
    void fcmp(Register rn, double imm);

    // Floating-point conversion
    void scvtf(Register rd, Register rn); // GPR to FPR
    void fcvtzs(Register rd, Register rn); // FPR to GPR


    // PC-relative address
    void adr(Register rd, uintptr_t target_address);
    void adrp(Register rd, uintptr_t target_address);

    // Load/Store instructions
    void ldr(Register rt, Register rn, int32_t offset = 0);
    void str(Register rt, Register rn, int32_t offset = 0);
    void ldr(Register rt, uintptr_t address);
    void str(Register rt, uintptr_t address);
    void ldur(Register rt, Register rn, int32_t offset = 0);
    void stur(Register rt, Register rn, int32_t offset = 0);
    void ldp(Register rt1, Register rt2, Register rn, int32_t offset, bool post_index = false);
    void stp(Register rt1, Register rt2, Register rn, int32_t offset, bool pre_index = false);
    
    // PC-relative load
    void ldr_literal(Register rt, int64_t offset);

    // Bitfield instructions
    void bfi(Register rd, Register rn, unsigned lsb, unsigned width);
    void sbfx(Register rd, Register rn, unsigned lsb, unsigned width);
    void ubfx(Register rd, Register rn, unsigned lsb, unsigned width);

    // System instructions
    void nop();
    void svc(uint16_t imm);

    // NEON Data Processing instructions
    void neon_add(Register rd, Register rn, Register rm, NeonArrangement arr);
    void neon_sub(Register rd, Register rn, Register rm, NeonArrangement arr);
    void neon_mul(Register rd, Register rn, Register rm, NeonArrangement arr);

    // NEON Load/Store instructions
    void neon_ldr(Register rt, Register rn, int32_t offset = 0);
    void neon_str(Register rt, Register rn, int32_t offset = 0);

    // Pseudo-code instructions
    void call_function(uintptr_t destination);
    void push(Register reg);
    void pop(Register reg);
    void load_constant(Register dest, uint64_t value);

    const std::vector<uint32_t>& get_code() const;
    std::vector<uint32_t>& get_code_mut();
    size_t get_code_size() const;
    uintptr_t get_current_address() const;

    static constexpr size_t ABS_JUMP_SIZE = 20; // 4 mov + 1 br
    static constexpr size_t ABS_CALL_SIZE = 28; // 1 stp + 4 mov + 1 blr + 1 ldp

private:
    uintptr_t current_address_;
    std::vector<uint32_t> code_;

    void emit(uint32_t instruction);
    uint32_t to_reg(Register reg);
    uint32_t to_cond(Condition cond);
    
    bool try_encode_logical_imm(uint64_t imm, uint32_t& out_N, uint32_t& out_imms, uint32_t& out_immr);
};

} // namespace ur::assembler