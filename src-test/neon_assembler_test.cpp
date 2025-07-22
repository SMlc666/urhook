#include "gtest/gtest.h"
#include "ur/assembler.h"
#include <capstone/capstone.h>
#include <string>
#include <vector>
#include <iostream>

// Helper function to disassemble code and return a vector of strings
std::vector<std::string> disassemble(const std::vector<uint32_t>& code, uint64_t address);

TEST(AssemblerNeonTest, NeonDataProcessing) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.neon_add(Register::Q0, Register::Q1, Register::Q2, NeonArrangement::B16);
    assembler.neon_sub(Register::Q3, Register::Q4, Register::Q5, NeonArrangement::H8);
    assembler.neon_mul(Register::Q6, Register::Q7, Register::Q8, NeonArrangement::S4);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 3);
    EXPECT_EQ(instructions[0], "add v0.16b, v1.16b, v2.16b");
    EXPECT_EQ(instructions[1], "sub v3.8h, v4.8h, v5.8h");
    EXPECT_EQ(instructions[2], "mul v6.4s, v7.4s, v8.4s");
}

TEST(AssemblerNeonTest, NeonLoadStore) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.neon_ldr(Register::Q0, Register::SP, 16);
    assembler.neon_str(Register::Q1, Register::SP, 32);
    assembler.neon_ldr(Register::D2, Register::SP, 8);
    assembler.neon_str(Register::D3, Register::SP, 24);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 4);
    EXPECT_EQ(instructions[0], "ldr q0, [sp, #0x10]");
    EXPECT_EQ(instructions[1], "str q1, [sp, #0x20]");
    EXPECT_EQ(instructions[2], "ldr d2, [sp, #8]");
    EXPECT_EQ(instructions[3], "str d3, [sp, #0x18]");
}
