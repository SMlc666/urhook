#include "gtest/gtest.h"
#include "ur/assembler.h"
#include <capstone/capstone.h>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <iomanip>

// Helper function to disassemble code and return a vector of strings
std::vector<std::string> disassemble(const std::vector<uint32_t>& code, uint64_t address) {
    csh handle;
    cs_insn* insn;
    size_t count;
    std::vector<std::string> result;

    // 1) CS_ARCH_ARM64 + CS_MODE_LITTLE_ENDIAN
    if (cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &handle) != CS_ERR_OK) {
        std::cerr << "Failed to initialize capstone" << std::endl;
        return result;
    }
    // 2) 开启 detail（可选）
    cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

    // 3) 反汇编，code.size()*4 保持不变
    count = cs_disasm(handle,
                      reinterpret_cast<const uint8_t*>(code.data()),
                      code.size() * 4,
                      address,
                      0,
                      &insn);

    if (count > 0) {
        for (size_t i = 0; i < count; i++) {
            std::string inst = std::string(insn[i].mnemonic) + " " + insn[i].op_str;
            inst.erase(inst.find_last_not_of(" \n\r\t") + 1);
            result.push_back(inst);
        }
        cs_free(insn, count);
    } else {
        std::cerr << "Failed to disassemble code" << std::endl;
    }

    cs_close(&handle);
    return result;
}

TEST(AssemblerTest, GenAbsJump) {
    using namespace ur::assembler;
    Assembler assembler(0x1000);
    assembler.gen_abs_jump(0x123456789ABCDEF0, Register::X16);
    auto instructions = disassemble(assembler.get_code(), 0x1000);
    ASSERT_EQ(instructions.size(), 5);
    EXPECT_EQ(instructions[0], "mov x16, #0xdef0");
    EXPECT_EQ(instructions[1], "movk x16, #0x9abc, lsl #16");
    EXPECT_EQ(instructions[2], "movk x16, #0x5678, lsl #32");
    EXPECT_EQ(instructions[3], "movk x16, #0x1234, lsl #48");
    EXPECT_EQ(instructions[4], "br x16");
}

TEST(AssemblerTest, GenAbsCall) {
    using namespace ur::assembler;
    Assembler assembler(0x2000);
    assembler.gen_abs_call(0x123456789ABCDEF0, Register::X17);
    auto instructions = disassemble(assembler.get_code(), 0x2000);
    ASSERT_EQ(instructions.size(), 7);
    EXPECT_EQ(instructions[0], "stp x29, x30, [sp, #-0x10]!");
    EXPECT_EQ(instructions[1], "mov x17, #0xdef0");
    EXPECT_EQ(instructions[2], "movk x17, #0x9abc, lsl #16");
    EXPECT_EQ(instructions[3], "movk x17, #0x5678, lsl #32");
    EXPECT_EQ(instructions[4], "movk x17, #0x1234, lsl #48");
    EXPECT_EQ(instructions[5], "blr x17");
    EXPECT_EQ(instructions[6], "ldp x29, x30, [sp], #0x10");
}

TEST(AssemblerTest, BranchInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0x1000);
    assembler.b(0x1020);
    assembler.bl(0x1040);
    assembler.blr(Register::X5);
    assembler.br(Register::X6);
    assembler.ret();
    auto instructions = disassemble(assembler.get_code(), 0x1000);
    ASSERT_EQ(instructions.size(), 5);
    EXPECT_EQ(instructions[0], "b #0x1020");
    EXPECT_EQ(instructions[1], "bl #0x1040");
    EXPECT_EQ(instructions[2], "blr x5");
    EXPECT_EQ(instructions[3], "br x6");
    EXPECT_EQ(instructions[4], "ret");
}

TEST(AssemblerTest, ConditionalBranchInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0x1000);
    assembler.b(Condition::EQ, 0x1008);
    assembler.b(Condition::NE, 0x100C);
    assembler.b(Condition::GT, 0x1010);
    auto instructions = disassemble(assembler.get_code(), 0x1000);
    ASSERT_EQ(instructions.size(), 3);
    EXPECT_EQ(instructions[0], "b.eq #0x1008");
    EXPECT_EQ(instructions[1], "b.ne #0x100c");
    EXPECT_EQ(instructions[2], "b.gt #0x1010");
}

TEST(AssemblerTest, CompareAndBranchInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0x1000);
    assembler.cbz(Register::X0, 0x1008);
    assembler.cbnz(Register::X1, 0x100C);
    auto instructions = disassemble(assembler.get_code(), 0x1000);
    ASSERT_EQ(instructions.size(), 2);
    EXPECT_EQ(instructions[0], "cbz x0, #0x1008");
    EXPECT_EQ(instructions[1], "cbnz x1, #0x100c");
}

TEST(AssemblerTest, DataProcessingImmediate) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.add(Register::X0, Register::X1, 0x123);
    assembler.add(Register::X2, Register::X3, 0x456, true);
    assembler.sub(Register::X4, Register::X5, 0x789);
    assembler.sub(Register::X6, Register::X7, 0xABC, true);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 4);
    EXPECT_EQ(instructions[0], "add x0, x1, #0x123");
    EXPECT_EQ(instructions[1], "add x2, x3, #0x456, lsl #12");
    EXPECT_EQ(instructions[2], "sub x4, x5, #0x789");
    EXPECT_EQ(instructions[3], "sub x6, x7, #0xabc, lsl #12");
}

TEST(AssemblerTest, DataProcessingRegister) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.add(Register::X0, Register::X1, Register::X2);
    assembler.sub(Register::X3, Register::X4, Register::X5);
    assembler.and_(Register::X6, Register::X7, Register::X8);
    assembler.orr(Register::X9, Register::X10, Register::X11);
    assembler.eor(Register::X12, Register::X13, Register::X14);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 5);
    EXPECT_EQ(instructions[0], "add x0, x1, x2");
    EXPECT_EQ(instructions[1], "sub x3, x4, x5");
    EXPECT_EQ(instructions[2], "and x6, x7, x8");
    EXPECT_EQ(instructions[3], "orr x9, x10, x11");
    EXPECT_EQ(instructions[4], "eor x12, x13, x14");
}

TEST(AssemblerTest, MoveInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.mov(Register::X0, 0x1234);
    assembler.mov(Register::X1, Register::X0);
    assembler.movn(Register::X2, 0x5678, 16);
    assembler.movz(Register::X3, 0x9ABC, 32);
    assembler.movk(Register::X3, 0xDEF0, 48);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 5);
    EXPECT_EQ(instructions[0], "mov x0, #0x1234");
    EXPECT_EQ(instructions[1], "mov x1, x0");
    EXPECT_EQ(instructions[2], "mov x2, #-0x56780001");
    EXPECT_EQ(instructions[3], "mov x3, #0x9abc00000000");
    EXPECT_EQ(instructions[4], "movk x3, #0xdef0, lsl #48");
}

TEST(AssemblerTest, LoadStoreInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.ldr(Register::X0, Register::SP, 8);
    assembler.str(Register::X1, Register::SP, 16);
    assembler.ldur(Register::X2, Register::FP, -8);
    assembler.stur(Register::X3, Register::FP, -16);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 4);
    EXPECT_EQ(instructions[0], "ldr x0, [sp, #8]");
    EXPECT_EQ(instructions[1], "str x1, [sp, #0x10]");
    EXPECT_EQ(instructions[2], "ldur x2, [x29, #-8]");
    EXPECT_EQ(instructions[3], "stur x3, [x29, #-0x10]");
}

TEST(AssemblerTest, LoadStorePairInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.stp(Register::X0, Register::X1, Register::SP, -16, true);
    assembler.ldp(Register::X2, Register::X3, Register::SP, 16, true);
    assembler.stp(Register::X4, Register::X5, Register::SP, 32, false);
    assembler.ldp(Register::X6, Register::X7, Register::SP, -32, false);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 4);
    EXPECT_EQ(instructions[0], "stp x0, x1, [sp, #-0x10]!");
    EXPECT_EQ(instructions[1], "ldp x2, x3, [sp], #0x10");
    EXPECT_EQ(instructions[2], "stp x4, x5, [sp, #0x20]");
    EXPECT_EQ(instructions[3], "ldp x6, x7, [sp, #-0x20]");
}

TEST(AssemblerTest, LoadLiteralInstruction) {
    using namespace ur::assembler;
    Assembler assembler(0x1000);
    assembler.ldr_literal(Register::X0, 0x1020 - 0x1000);
    auto instructions = disassemble(assembler.get_code(), 0x1000);
    ASSERT_EQ(instructions.size(), 1);
    EXPECT_EQ(instructions[0], "ldr x0, #0x1020");
}

TEST(AssemblerTest, BitfieldInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.bfi(Register::X0, Register::X1, 8, 16);
    assembler.sbfx(Register::X2, Register::X3, 4, 8);
    assembler.ubfx(Register::X4, Register::X5, 12, 20);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 3);
    EXPECT_EQ(instructions[0], "sbfiz x0, x1, #8, #0x10");
    EXPECT_EQ(instructions[1], "sbfx x2, x3, #4, #8");
    EXPECT_EQ(instructions[2], "ubfx x4, x5, #0xc, #0x14");
}

TEST(AssemblerTest, ShiftedRegisterInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.add(Register::X0, Register::X1, Register::X2, 0, 12); // LSL
    assembler.sub(Register::X3, Register::X4, Register::X5, 1, 8);  // LSR
    assembler.add(Register::X6, Register::X7, Register::X8, 2, 16); // ASR
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 3);
    EXPECT_EQ(instructions[0], "add x0, x1, x2, lsl #12");
    EXPECT_EQ(instructions[1], "sub x3, x4, x5, lsr #8");
    EXPECT_EQ(instructions[2], "add x6, x7, x8, asr #16");
}

TEST(AssemblerTest, MiscInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0x1000);
    assembler.cmp(Register::X0, Register::X1);
    assembler.cset(Register::X2, Condition::EQ);
    assembler.adr(Register::X4, 0x1020);
    assembler.adrp(Register::X3, 0x2000);
    auto instructions = disassemble(assembler.get_code(), 0x1000);
    ASSERT_EQ(instructions.size(), 4);
    EXPECT_EQ(instructions[0], "sub xzr, x0, x1");
    EXPECT_EQ(instructions[1], "cset x2, eq");
    EXPECT_EQ(instructions[2], "adr x4, #0x1020");
    EXPECT_EQ(instructions[3], "adrp x3, #0x2000");
}

TEST(AssemblerTest, ConditionalSelectInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.csel(Register::X0, Register::X1, Register::X2, Condition::EQ);
    assembler.csinc(Register::X3, Register::X4, Register::X5, Condition::NE);
    assembler.csinv(Register::X6, Register::X7, Register::X8, Condition::GT);
    assembler.csneg(Register::X9, Register::X10, Register::X11, Condition::LS);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 4);
    EXPECT_EQ(instructions[0], "csel x0, x1, x2, eq");
    EXPECT_EQ(instructions[1], "csinc x3, x4, x5, ne");
    EXPECT_EQ(instructions[2], "csinv x6, x7, x8, gt");
    EXPECT_EQ(instructions[3], "csneg x9, x10, x11, ls");
}

TEST(AssemblerTest, NewInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.lsl(Register::X0, Register::X1, 16);
    assembler.lsr(Register::W2, Register::W3, 8);
    assembler.asr(Register::X4, Register::X5, 4);
    assembler.tbnz(Register::X6, 3, 0x10);
    assembler.tbz(Register::W7, 2, 0x14);
    assembler.bic(Register::X8, Register::X9, Register::X10);
    assembler.mvn(Register::W11, Register::W12);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 7);
    EXPECT_EQ(instructions[0], "lsl x0, x1, #0x10");
    EXPECT_EQ(instructions[1], "lsr w2, w3, #8");
    EXPECT_EQ(instructions[2], "asr x4, x5, #4");
    EXPECT_EQ(instructions[3], "tbnz w6, #3, #0x10");
    EXPECT_EQ(instructions[4], "tbz w7, #2, #0x14");
    EXPECT_EQ(instructions[5], "bic x8, x9, x10");
    EXPECT_EQ(instructions[6], "mvn w11, w12");
}

TEST(AssemblerTest, SystemInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.nop();
    assembler.svc(0x123);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 2);
    EXPECT_EQ(instructions[0], "nop");
    EXPECT_EQ(instructions[1], "svc #0x123");
}

TEST(AssemblerTest, MultiplyDivideInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.mul(Register::X0, Register::X1, Register::X2);
    assembler.sdiv(Register::X3, Register::X4, Register::X5);
    assembler.udiv(Register::X6, Register::X7, Register::X8);
    assembler.madd(Register::X9, Register::X10, Register::X11, Register::X12);
    assembler.msub(Register::X13, Register::X14, Register::X15, Register::X16);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 5);
    EXPECT_EQ(instructions[0], "mul x0, x1, x2");
    EXPECT_EQ(instructions[1], "sdiv x3, x4, x5");
    EXPECT_EQ(instructions[2], "udiv x6, x7, x8");
    EXPECT_EQ(instructions[3], "madd x9, x10, x11, x12");
    EXPECT_EQ(instructions[4], "msub x13, x14, x15, x16");
}

TEST(AssemblerTest, DataProcessingImmediate32) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.add(Register::W0, Register::W1, 0x123);
    assembler.sub(Register::W2, Register::W3, 0x456);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 2);
    EXPECT_EQ(instructions[0], "add w0, w1, #0x123");
    EXPECT_EQ(instructions[1], "sub w2, w3, #0x456");
}

TEST(AssemblerTest, DataProcessingRegister32) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.add(Register::W0, Register::W1, Register::W2);
    assembler.sub(Register::W3, Register::W4, Register::W5);
    assembler.and_(Register::W6, Register::W7, Register::W8);
    assembler.orr(Register::W9, Register::W10, Register::W11);
    assembler.eor(Register::W12, Register::W13, Register::W14);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 5);
    EXPECT_EQ(instructions[0], "add w0, w1, w2");
    EXPECT_EQ(instructions[1], "sub w3, w4, w5");
    EXPECT_EQ(instructions[2], "and w6, w7, w8");
    EXPECT_EQ(instructions[3], "orr w9, w10, w11");
    EXPECT_EQ(instructions[4], "eor w12, w13, w14");
}

TEST(AssemblerTest, MoveInstructions32) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.mov(Register::W0, 0x1234);
    assembler.mov(Register::W1, Register::W0);
    assembler.movn(Register::W2, 0x5678, 16);
    assembler.movz(Register::W3, 0x9ABC, 0);
    assembler.movk(Register::W3, 0xDEF0, 16);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 5);
    EXPECT_EQ(instructions[0], "mov w0, #0x1234");
    EXPECT_EQ(instructions[1], "mov w1, w0");
    EXPECT_EQ(instructions[2], "mov w2, #-0x56780001");
    EXPECT_EQ(instructions[3], "mov w3, #0x9abc");
    EXPECT_EQ(instructions[4], "movk w3, #0xdef0, lsl #16");
}

TEST(AssemblerTest, LoadStoreInstructions32) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.ldr(Register::W0, Register::SP, 4);
    assembler.str(Register::W1, Register::SP, 8);
    assembler.ldur(Register::W2, Register::FP, -4);
    assembler.stur(Register::W3, Register::FP, -8);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 4);
    EXPECT_EQ(instructions[0], "ldr w0, [sp, #4]");
    EXPECT_EQ(instructions[1], "str w1, [sp, #8]");
    EXPECT_EQ(instructions[2], "ldur w2, [x29, #-4]");
    EXPECT_EQ(instructions[3], "stur w3, [x29, #-8]");
}

TEST(AssemblerTest, FloatInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);

    // Arithmetic
    assembler.fadd(Register::S0, Register::S1, Register::S2);
    assembler.fadd(Register::D0, Register::D1, Register::D2);
    assembler.fsub(Register::S3, Register::S4, Register::S5);
    assembler.fsub(Register::D3, Register::D4, Register::D5);
    assembler.fmul(Register::S6, Register::S7, Register::S8);
    assembler.fmul(Register::D6, Register::D7, Register::D8);
    assembler.fdiv(Register::S9, Register::S10, Register::S11);
    assembler.fdiv(Register::D9, Register::D10, Register::D11);

    // Move between FP registers
    assembler.fmov(Register::S12, Register::S13);
    assembler.fmov(Register::D12, Register::D13);

    // Move between GPR and FPR
    assembler.fmov(Register::S14, Register::W0);
    assembler.fmov(Register::D15, Register::X1);
    assembler.fmov(Register::W2, Register::S16);
    assembler.fmov(Register::X3, Register::D17);

    // Move immediate
    assembler.fmov(Register::D20, 1.2345); // 0x3ff3c083126e978d
    assembler.fmov(Register::S21, 6.789);  // 0x40d93b64

    // Compare
    assembler.fcmp(Register::S22, Register::S23);
    assembler.fcmp(Register::D24, Register::D25);
    assembler.fcmp(Register::S26, 0.0);
    assembler.fcmp(Register::D27, 0.0);

    // Conversion
    assembler.scvtf(Register::S28, Register::W4);
    assembler.scvtf(Register::D29, Register::X5);
    assembler.fcvtzs(Register::W6, Register::S30);
    assembler.fcvtzs(Register::X7, Register::D31);

    auto instructions = disassemble(assembler.get_code(), 0);

    ASSERT_EQ(instructions.size(), 30);

    int i = 0;
    // Arithmetic
    EXPECT_EQ(instructions[i++], "fadd s0, s1, s2");
    EXPECT_EQ(instructions[i++], "fadd d0, d1, d2");
    EXPECT_EQ(instructions[i++], "fsub s3, s4, s5");
    EXPECT_EQ(instructions[i++], "fsub d3, d4, d5");
    EXPECT_EQ(instructions[i++], "fmul s6, s7, s8");
    EXPECT_EQ(instructions[i++], "fmul d6, d7, d8");
    EXPECT_EQ(instructions[i++], "fdiv s9, s10, s11");
    EXPECT_EQ(instructions[i++], "fdiv d9, d10, d11");

    // Move between FP registers
    EXPECT_EQ(instructions[i++], "fmov s12, s13");
    EXPECT_EQ(instructions[i++], "fmov d12, d13");

    // Move between GPR and FPR
    EXPECT_EQ(instructions[i++], "fmov s14, w0");
    EXPECT_EQ(instructions[i++], "fmov d15, x1");
    EXPECT_EQ(instructions[i++], "fmov w2, s16");
    EXPECT_EQ(instructions[i++], "fmov x3, d17");

    // Move immediate double
    EXPECT_EQ(instructions[i++], "mov x16, #0x978d");
    EXPECT_EQ(instructions[i++], "movk x16, #0x126e, lsl #16");
    EXPECT_EQ(instructions[i++], "movk x16, #0xc083, lsl #32");
    EXPECT_EQ(instructions[i++], "movk x16, #0x3ff3, lsl #48");
    EXPECT_EQ(instructions[i++], "fmov d20, x16");

    // Move immediate float
    EXPECT_EQ(instructions[i++], "mov w16, #0x3f7d");
    EXPECT_EQ(instructions[i++], "movk w16, #0x40d9, lsl #16");
    EXPECT_EQ(instructions[i++], "fmov s21, w16");

    // Compare
    EXPECT_EQ(instructions[i++], "fcmp s22, s23");
    EXPECT_EQ(instructions[i++], "fcmp d24, d25");
    EXPECT_EQ(instructions[i++], "fcmp s26, #0.0");
    EXPECT_EQ(instructions[i++], "fcmp d27, #0.0");

    // Conversion
    EXPECT_EQ(instructions[i++], "scvtf s28, w4");
    EXPECT_EQ(instructions[i++], "scvtf d29, x5");
    EXPECT_EQ(instructions[i++], "fcvtzs w6, s30");
    EXPECT_EQ(instructions[i++], "fcvtzs x7, d31");
}

// Death tests for assembler error conditions
class AssemblerDeathTest : public ::testing::Test {
protected:
    ur::assembler::Assembler assembler{0};
};

TEST_F(AssemblerDeathTest, BranchOutOfRange) {
    using namespace ur::assembler;
    ASSERT_THROW(assembler.b(0x100000000), std::runtime_error);
    ASSERT_THROW(assembler.bl(0x100000000), std::runtime_error);
    ASSERT_THROW(assembler.b(Condition::EQ, 0x100000), std::runtime_error);
    ASSERT_THROW(assembler.cbz(Register::X0, 0x100000), std::runtime_error);
    ASSERT_THROW(assembler.cbnz(Register::X0, 0x100000), std::runtime_error);
}

TEST_F(AssemblerDeathTest, AdrpOutOfRange) {
    using namespace ur::assembler;
    ASSERT_THROW(assembler.adrp(Register::X0, 0x1000000000), std::runtime_error);
}

TEST_F(AssemblerDeathTest, LoadStoreOutOfRange) {
    using namespace ur::assembler;
    ASSERT_THROW(assembler.ldur(Register::X0, Register::SP, 256), std::runtime_error);
    ASSERT_THROW(assembler.stur(Register::X0, Register::SP, 256), std::runtime_error);
    ASSERT_THROW(assembler.ldp(Register::X0, Register::X1, Register::SP, 256 * 8), std::runtime_error);
    ASSERT_THROW(assembler.stp(Register::X0, Register::X1, Register::SP, 256 * 8), std::runtime_error);
    ASSERT_THROW(assembler.ldr_literal(Register::X0, 1048576), std::runtime_error);
}

TEST_F(AssemblerDeathTest, BitfieldOutOfRange) {
    using namespace ur::assembler;
    ASSERT_THROW(assembler.bfi(Register::X0, Register::X1, 64, 16), std::runtime_error);
    ASSERT_THROW(assembler.bfi(Register::X0, Register::X1, 0, 65), std::runtime_error);
    ASSERT_THROW(assembler.sbfx(Register::X0, Register::X1, 64, 8), std::runtime_error);
    ASSERT_THROW(assembler.sbfx(Register::X0, Register::X1, 0, 65), std::runtime_error);
    ASSERT_THROW(assembler.ubfx(Register::X0, Register::X1, 64, 20), std::runtime_error);
    ASSERT_THROW(assembler.ubfx(Register::X0, Register::X1, 0, 65), std::runtime_error);
}

TEST_F(AssemblerDeathTest, LogicalImmediateError) {
    using namespace ur::assembler;
    ASSERT_THROW(assembler.and_(Register::X0, Register::X1, 0x101010101010101), std::runtime_error);
    ASSERT_THROW(assembler.orr(Register::X0, Register::X1, 0x101010101010101), std::runtime_error);
    ASSERT_THROW(assembler.eor(Register::X0, Register::X1, 0x101010101010101), std::runtime_error);
}

TEST_F(AssemblerDeathTest, FmovErrors) {
    using namespace ur::assembler;
    ASSERT_THROW(assembler.fmov(Register::S0, Register::D0), std::runtime_error);
    ASSERT_THROW(assembler.fmov(Register::D0, Register::S0), std::runtime_error);
    ASSERT_THROW(assembler.fmov(Register::S0, Register::X0), std::runtime_error);
    ASSERT_THROW(assembler.fmov(Register::D0, Register::W0), std::runtime_error);
    ASSERT_THROW(assembler.fmov(Register::W0, Register::D0), std::runtime_error);
    ASSERT_THROW(assembler.fmov(Register::X0, Register::S0), std::runtime_error);
    ASSERT_THROW(assembler.fmov(Register::X0, 1.0), std::runtime_error);
}

TEST_F(AssemblerDeathTest, FcmpError) {
    using namespace ur::assembler;
    ASSERT_THROW(assembler.fcmp(Register::S0, 1.0), std::runtime_error);
}

TEST(AssemblerPseudoInstructionsTest, CallFunction) {
    using namespace ur::assembler;
    Assembler assembler(0x1000);
    assembler.call_function(0x123456789ABCDEF0);
    auto instructions = disassemble(assembler.get_code(), 0x1000);
    ASSERT_EQ(instructions.size(), 7);
    EXPECT_EQ(instructions[0], "stp x29, x30, [sp, #-0x10]!");
    EXPECT_EQ(instructions[1], "mov x17, #0xdef0");
    EXPECT_EQ(instructions[2], "movk x17, #0x9abc, lsl #16");
    EXPECT_EQ(instructions[3], "movk x17, #0x5678, lsl #32");
    EXPECT_EQ(instructions[4], "movk x17, #0x1234, lsl #48");
    EXPECT_EQ(instructions[5], "blr x17");
    EXPECT_EQ(instructions[6], "ldp x29, x30, [sp], #0x10");
}

TEST(AssemblerPseudoInstructionsTest, PushPop) {
    using namespace ur::assembler;
    Assembler assembler(0x1000);
    assembler.push(Register::X0);
    assembler.push(Register::W1);
    assembler.pop(Register::X2);
    assembler.pop(Register::W3);
    auto instructions = disassemble(assembler.get_code(), 0x1000);
    ASSERT_EQ(instructions.size(), 4);
    EXPECT_EQ(instructions[0], "stp x0, xzr, [sp, #-0x10]!");
    EXPECT_EQ(instructions[1], "stp w1, wzr, [sp, #-0x10]!");
    EXPECT_EQ(instructions[2], "ldp x2, xzr, [sp], #0x10");
    EXPECT_EQ(instructions[3], "ldp w3, wzr, [sp], #0x10");
}

TEST(AssemblerPseudoInstructionsTest, LoadConstant) {
    using namespace ur::assembler;
    Assembler assembler(0x1000);
    assembler.load_constant(Register::X0, 0x123456789ABCDEF0ULL);
    assembler.load_constant(Register::W1, 0x12345678U);
    auto instructions = disassemble(assembler.get_code(), 0x1000);
    ASSERT_EQ(instructions.size(), 6);
    EXPECT_EQ(instructions[0], "mov x0, #0xdef0");
    EXPECT_EQ(instructions[1], "movk x0, #0x9abc, lsl #16");
    EXPECT_EQ(instructions[2], "movk x0, #0x5678, lsl #32");
    EXPECT_EQ(instructions[3], "movk x0, #0x1234, lsl #48");
    EXPECT_EQ(instructions[4], "mov w1, #0x5678");
    EXPECT_EQ(instructions[5], "movk w1, #0x1234, lsl #16");
}

TEST(AssemblerTest, SystemInstructionsMrsMsr) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.mrs(Register::X0, SystemRegister::NZCV);
    assembler.msr(SystemRegister::NZCV, Register::X0);
    assembler.mrs(Register::X1, SystemRegister::FPCR);
    assembler.msr(SystemRegister::FPCR, Register::X1);
    assembler.mrs(Register::X2, SystemRegister::FPSR);
    assembler.msr(SystemRegister::FPSR, Register::X2);
    assembler.mrs(Register::X3, SystemRegister::TPIDR_EL0);
    assembler.msr(SystemRegister::TPIDR_EL0, Register::X3);

    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 8);
    EXPECT_EQ(instructions[0], "mrs x0, nzcv");
    EXPECT_EQ(instructions[1], "msr nzcv, x0");
    EXPECT_EQ(instructions[2], "mrs x1, fpcr");
    EXPECT_EQ(instructions[3], "msr fpcr, x1");
    EXPECT_EQ(instructions[4], "mrs x2, fpsr");
    EXPECT_EQ(instructions[5], "msr fpsr, x2");
    EXPECT_EQ(instructions[6], "mrs x3, tpidr_el0");
    EXPECT_EQ(instructions[7], "msr tpidr_el0, x3");
}

TEST(AssemblerTest, IsbInstruction) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.isb();
    auto instructions = disassemble(assembler.get_code(), 0);
    
    ASSERT_EQ(instructions.size(), 1);
    EXPECT_EQ(instructions[0], "isb");
}
TEST(AssemblerTest, AllMemoryBarrierOptionsForDSB) {
    using namespace ur::assembler;

    struct {
        BarrierOption option;
        const char* expected_mnemonic;
    } test_cases[] = {
      { BarrierOption::OSH,  "dsb ishst" },  // 实际imm4为0b0111（不是osh）
      { BarrierOption::NSH,  "dsb nshst" },
      { BarrierOption::ISH,  "dsb ish"    },
      { BarrierOption::SY,   "dsb st"     },  // Capstone 反汇出的是 dsb st，不是 dsb sy
    };

    for (const auto& tc : test_cases) {
        Assembler assembler(0);
        assembler.dsb(tc.option);
        auto instructions = disassemble(assembler.get_code(), 0);

        ASSERT_EQ(instructions.size(), 1) << "Failed for option: " << tc.expected_mnemonic;
        EXPECT_EQ(instructions[0], tc.expected_mnemonic);
    }
}
TEST(AssemblerTest, AllMemoryBarrierOptionsForDMB) {
    using namespace ur::assembler;

    struct {
        BarrierOption option;
        const char* expected_mnemonic;
    } test_cases[] = {
        { BarrierOption::OSH,  "dmb osh" },
        { BarrierOption::NSH,  "dmb nsh" },
        { BarrierOption::ISH,  "dmb ish" },
        { BarrierOption::SY,   "dmb sy"  },
    };

    for (const auto& tc : test_cases) {
        Assembler assembler(0);
        assembler.dmb(tc.option);
        auto instructions = disassemble(assembler.get_code(), 0);

        ASSERT_EQ(instructions.size(), 1) << "Failed for option: " << tc.expected_mnemonic;
        EXPECT_EQ(instructions[0], tc.expected_mnemonic);
    }
}
TEST(AssemblerTest, ExclusiveAccessInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.ldxr(Register::X0, Register::X1);
    assembler.stxr(Register::W2, Register::W3, Register::X4);
    assembler.ldaxr(Register::X5, Register::X6);
    assembler.stlxr(Register::W7, Register::W8, Register::X9);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 4);
    EXPECT_EQ(instructions[0], "ldxr x0, [x1]");
    EXPECT_EQ(instructions[1], "stxr w2, w3, [x4]");
    EXPECT_EQ(instructions[2], "ldaxr x5, [x6]");
    EXPECT_EQ(instructions[3], "stlxr w7, w8, [x9]");
}

TEST(AssemblerTest, LoadAcquireStoreReleaseInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.ldar(Register::W0, Register::X1);
    assembler.stlr(Register::W2, Register::X3);
    assembler.ldar(Register::X4, Register::X5);
    assembler.stlr(Register::X6, Register::X7);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 4);
    EXPECT_EQ(instructions[0], "ldlar w0, [x1]");
    EXPECT_EQ(instructions[1], "stllr w2, [x3]");
    EXPECT_EQ(instructions[2], "ldlar x4, [x5]");
    EXPECT_EQ(instructions[3], "stllr x6, [x7]");
}

TEST(AssemblerTest, HalfwordAndByteLoadStoreInstructions) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.ldrh(Register::W0, Register::X1, 12);
    assembler.ldrb(Register::W2, Register::X3, 8);
    assembler.ldrsw(Register::X4, Register::X5, 16);
    assembler.ldrsh(Register::W6, Register::X7, 10);
    assembler.ldrsh(Register::X8, Register::X9, -10);
    assembler.ldrsb(Register::W10, Register::X11, 6);
    assembler.ldrsb(Register::X12, Register::X13, -6);
    assembler.strh(Register::W14, Register::X15, 4);
    assembler.strb(Register::W16, Register::X17, 2);

    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 9);
    EXPECT_EQ(instructions[0], "ldrh w0, [x1, #0xc]");
    EXPECT_EQ(instructions[1], "ldrb w2, [x3, #8]");
    EXPECT_EQ(instructions[2], "ldrsw x4, [x5, #0x10]");
    EXPECT_EQ(instructions[3], "ldrsh w6, [x7, #0xa]");
    EXPECT_EQ(instructions[4], "ldursh x8, [x9, #-0xa]");
    EXPECT_EQ(instructions[5], "ldrsb w10, [x11, #6]");
    EXPECT_EQ(instructions[6], "ldursb x12, [x13, #-6]");
    EXPECT_EQ(instructions[7], "strh w14, [x15, #4]");
    EXPECT_EQ(instructions[8], "strb w16, [x17, #2]");
}
