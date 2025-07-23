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
    assembler.neon_mul(Register::Q6, Register::Q7, Register::Q8, NeonArrangement::S4);
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 2);
    EXPECT_EQ(instructions[0], "add v0.16b, v1.16b, v2.16b");
    EXPECT_EQ(instructions[1], "mul v6.4s, v7.4s, v8.4s");
}

TEST(AssemblerNeonTest, NeonLogicAndCompare) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.neon_cmeq(Register::Q9, Register::Q10, Register::Q11, NeonArrangement::S4);
    assembler.neon_cmgt(Register::Q12, Register::Q13, Register::Q14, NeonArrangement::S4);
    assembler.neon_cmge(Register::Q15, Register::Q16, Register::Q17, NeonArrangement::S4);

    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 3);
    EXPECT_EQ(instructions[0], "cmeq v9.4s, v10.4s, v11.4s");
    EXPECT_EQ(instructions[1], "cmgt v12.4s, v13.4s, v14.4s");
    EXPECT_EQ(instructions[2], "cmge v15.4s, v16.4s, v17.4s");
}

TEST(AssemblerNeonTest, NeonFloatingPoint) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.neon_fadd(Register::Q0, Register::Q1, Register::Q2, NeonArrangement::S4);
    assembler.neon_fdiv(Register::Q9, Register::Q10, Register::Q11, NeonArrangement::D2);
    assembler.neon_fcmeq(Register::Q12, Register::Q13, Register::Q14, NeonArrangement::S4);
    
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 3);
    EXPECT_EQ(instructions[0], "fadd v0.4s, v1.4s, v2.4s");
    EXPECT_EQ(instructions[1], "fdiv v9.2d, v10.2d, v11.2d");
    EXPECT_EQ(instructions[2], "fcmeq v12.4s, v13.4s, v14.4s");
 
}

TEST(AssemblerNeonTest, NeonLoadStore) {
    using namespace ur::assembler;
    Assembler assembler(0);
    assembler.neon_str(Register::D1, Register::X1, 8);
    
    auto instructions = disassemble(assembler.get_code(), 0);
    ASSERT_EQ(instructions.size(), 1);
    EXPECT_EQ(instructions[0], "str d1, [x1, #8]");
}

