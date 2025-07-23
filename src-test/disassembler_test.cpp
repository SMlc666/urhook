#include "gtest/gtest.h"
#include "ur/disassembler.h"
#include "ur/assembler.h"
#include <sstream>
#include <iomanip>

// Helper function to format address for comparison
static std::string format_address(uint64_t addr) {
    std::stringstream ss;
    ss << "0x" << std::hex << addr;
    return ss.str();
}

TEST(DisassemblerTest, NopInstruction) {
    ur::assembler::Assembler assembler(0x1000);
    assembler.nop();
    const auto& assembled_code = assembler.get_code();
    const uint8_t* code_bytes = reinterpret_cast<const uint8_t*>(assembled_code.data());
    size_t code_size = assembled_code.size() * sizeof(uint32_t);

    auto disassembler = ur::disassembler::CreateAArch64Disassembler();
    auto instructions = disassembler->Disassemble(0x1000, code_bytes, code_size, 1);

    ASSERT_EQ(instructions.size(), 1);
    EXPECT_EQ(instructions[0].id, ur::disassembler::InstructionId::NOP);
    EXPECT_EQ(instructions[0].group, ur::disassembler::InstructionGroup::SYSTEM);
    EXPECT_EQ(instructions[0].mnemonic, "nop");
    EXPECT_EQ(instructions[0].op_str, "");
    EXPECT_TRUE(instructions[0].operands.empty());
}

TEST(DisassemblerTest, RetInstruction) {
    ur::assembler::Assembler assembler(0x1000);
    assembler.ret();
    const auto& assembled_code = assembler.get_code();
    const uint8_t* code_bytes = reinterpret_cast<const uint8_t*>(assembled_code.data());
    size_t code_size = assembled_code.size() * sizeof(uint32_t);

    auto disassembler = ur::disassembler::CreateAArch64Disassembler();
    auto instructions = disassembler->Disassemble(0x1000, code_bytes, code_size, 1);

    ASSERT_EQ(instructions.size(), 1);
    EXPECT_EQ(instructions[0].id, ur::disassembler::InstructionId::RET);
    EXPECT_EQ(instructions[0].group, ur::disassembler::InstructionGroup::JUMP);
    EXPECT_EQ(instructions[0].mnemonic, "ret");
    EXPECT_TRUE(instructions[0].operands.empty());
}

TEST(DisassemblerTest, BranchInstructions) {
    uint64_t start_addr = 0x2000;
    ur::assembler::Assembler assembler(start_addr);
    
    uint64_t target_b = 0x2020;
    uint64_t target_bl = 0x1F00;

    assembler.b(target_b);
    assembler.bl(target_bl);
    assembler.br(ur::assembler::Register::X10);
    assembler.blr(ur::assembler::Register::X11);


    const auto& assembled_code = assembler.get_code();
    const uint8_t* code_bytes = reinterpret_cast<const uint8_t*>(assembled_code.data());
    size_t code_size = assembled_code.size() * sizeof(uint32_t);

    auto disassembler = ur::disassembler::CreateAArch64Disassembler();
    auto instructions = disassembler->Disassemble(start_addr, code_bytes, code_size, 4);

    ASSERT_EQ(instructions.size(), 4);

    // B
    EXPECT_EQ(instructions[0].id, ur::disassembler::InstructionId::B);
    EXPECT_EQ(instructions[0].group, ur::disassembler::InstructionGroup::JUMP);
    EXPECT_EQ(instructions[0].mnemonic, "b");
    EXPECT_EQ(instructions[0].op_str, format_address(target_b));
    EXPECT_TRUE(instructions[0].is_pc_relative);
    ASSERT_EQ(instructions[0].operands.size(), 1);
    EXPECT_EQ(instructions[0].operands[0].type, ur::disassembler::OperandType::IMMEDIATE);
    EXPECT_EQ(std::get<int64_t>(instructions[0].operands[0].value), target_b);

    // BL
    EXPECT_EQ(instructions[1].id, ur::disassembler::InstructionId::BL);
    EXPECT_EQ(instructions[1].group, ur::disassembler::InstructionGroup::JUMP);
    EXPECT_EQ(instructions[1].mnemonic, "bl");
    EXPECT_EQ(instructions[1].op_str, format_address(target_bl));
    EXPECT_TRUE(instructions[1].is_pc_relative);
    ASSERT_EQ(instructions[1].operands.size(), 1);
    EXPECT_EQ(instructions[1].operands[0].type, ur::disassembler::OperandType::IMMEDIATE);
    EXPECT_EQ(std::get<int64_t>(instructions[1].operands[0].value), target_bl);

    // BR
    EXPECT_EQ(instructions[2].id, ur::disassembler::InstructionId::BR);
    EXPECT_EQ(instructions[2].group, ur::disassembler::InstructionGroup::JUMP);
    EXPECT_EQ(instructions[2].mnemonic, "br");
    EXPECT_EQ(instructions[2].op_str, "x10");
    ASSERT_EQ(instructions[2].operands.size(), 1);
    EXPECT_EQ(instructions[2].operands[0].type, ur::disassembler::OperandType::REGISTER);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[2].operands[0].value), ur::assembler::Register::X10);

    // BLR
    EXPECT_EQ(instructions[3].id, ur::disassembler::InstructionId::BLR);
    EXPECT_EQ(instructions[3].group, ur::disassembler::InstructionGroup::JUMP);
    EXPECT_EQ(instructions[3].mnemonic, "blr");
    EXPECT_EQ(instructions[3].op_str, "x11");
    ASSERT_EQ(instructions[3].operands.size(), 1);
    EXPECT_EQ(instructions[3].operands[0].type, ur::disassembler::OperandType::REGISTER);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[3].operands[0].value), ur::assembler::Register::X11);
}

TEST(DisassemblerTest, AddSubInstructions) {
    ur::assembler::Assembler assembler(0x3000);
    assembler.add(ur::assembler::Register::X0, ur::assembler::Register::X1, 16);
    assembler.sub(ur::assembler::Register::W2, ur::assembler::Register::W3, 32, true);
    assembler.add(ur::assembler::Register::SP, ur::assembler::Register::SP, 16);


    const auto& assembled_code = assembler.get_code();
    const uint8_t* code_bytes = reinterpret_cast<const uint8_t*>(assembled_code.data());
    size_t code_size = assembled_code.size() * sizeof(uint32_t);

    auto disassembler = ur::disassembler::CreateAArch64Disassembler();
    auto instructions = disassembler->Disassemble(0x3000, code_bytes, code_size, 3);

    ASSERT_EQ(instructions.size(), 3);

    // ADD x0, x1, #16
    EXPECT_EQ(instructions[0].id, ur::disassembler::InstructionId::ADD);
    EXPECT_EQ(instructions[0].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instructions[0].mnemonic, "add");
    EXPECT_EQ(instructions[0].op_str, "x0, x1, #16");
    ASSERT_EQ(instructions[0].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[0].operands[0].value), ur::assembler::Register::X0);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[0].operands[1].value), ur::assembler::Register::X1);
    EXPECT_EQ(std::get<int64_t>(instructions[0].operands[2].value), 16);

    // SUB w2, w3, #32, lsl #12
    EXPECT_EQ(instructions[1].id, ur::disassembler::InstructionId::SUB);
    EXPECT_EQ(instructions[1].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instructions[1].mnemonic, "sub");
    EXPECT_EQ(instructions[1].op_str, "w2, w3, #32, lsl #12");
    ASSERT_EQ(instructions[1].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[1].operands[0].value), ur::assembler::Register::W2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[1].operands[1].value), ur::assembler::Register::W3);
    EXPECT_EQ(std::get<int64_t>(instructions[1].operands[2].value), 32 << 12);

    // ADD sp, sp, #16
    EXPECT_EQ(instructions[2].id, ur::disassembler::InstructionId::ADD);
    EXPECT_EQ(instructions[2].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instructions[2].mnemonic, "add");
    EXPECT_EQ(instructions[2].op_str, "sp, sp, #16");
    ASSERT_EQ(instructions[2].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[2].operands[0].value), ur::assembler::Register::SP);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[2].operands[1].value), ur::assembler::Register::SP);
    EXPECT_EQ(std::get<int64_t>(instructions[2].operands[2].value), 16);
}

TEST(DisassemblerTest, DataProcessingRegisterInstructions) {
    ur::assembler::Assembler assembler(0x3100);
    assembler.add(ur::assembler::Register::X0, ur::assembler::Register::X1, ur::assembler::Register::X2);
    assembler.sub(ur::assembler::Register::W3, ur::assembler::Register::W4, ur::assembler::Register::W5);
    assembler.and_(ur::assembler::Register::X6, ur::assembler::Register::X7, ur::assembler::Register::X8);
    assembler.orr(ur::assembler::Register::W9, ur::assembler::Register::W10, ur::assembler::Register::W11);
    assembler.eor(ur::assembler::Register::X12, ur::assembler::Register::X13, ur::assembler::Register::X14);
    assembler.mov(ur::assembler::Register::X15, ur::assembler::Register::X16);

    const auto& code = assembler.get_code();
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(code.data());
    auto dis = ur::disassembler::CreateAArch64Disassembler();
    auto instrs = dis->Disassemble(0x3100, bytes, code.size() * 4, 6);

    ASSERT_EQ(instrs.size(), 6);
    
    // ADD
    EXPECT_EQ(instrs[0].id, ur::disassembler::InstructionId::ADD);
    EXPECT_EQ(instrs[0].op_str, "x0, x1, x2");
    ASSERT_EQ(instrs[0].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[0].operands[0].value), ur::assembler::Register::X0);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[0].operands[1].value), ur::assembler::Register::X1);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[0].operands[2].value), ur::assembler::Register::X2);

    // SUB
    EXPECT_EQ(instrs[1].id, ur::disassembler::InstructionId::SUB);
    EXPECT_EQ(instrs[1].op_str, "w3, w4, w5");
    ASSERT_EQ(instrs[1].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[1].operands[0].value), ur::assembler::Register::W3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[1].operands[1].value), ur::assembler::Register::W4);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[1].operands[2].value), ur::assembler::Register::W5);

    // AND
    EXPECT_EQ(instrs[2].id, ur::disassembler::InstructionId::AND);
    EXPECT_EQ(instrs[2].op_str, "x6, x7, x8");
    ASSERT_EQ(instrs[2].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[2].operands[0].value), ur::assembler::Register::X6);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[2].operands[1].value), ur::assembler::Register::X7);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[2].operands[2].value), ur::assembler::Register::X8);

    // ORR
    EXPECT_EQ(instrs[3].id, ur::disassembler::InstructionId::ORR);
    EXPECT_EQ(instrs[3].op_str, "w9, w10, w11");
    ASSERT_EQ(instrs[3].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[3].operands[0].value), ur::assembler::Register::W9);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[3].operands[1].value), ur::assembler::Register::W10);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[3].operands[2].value), ur::assembler::Register::W11);

    // EOR
    EXPECT_EQ(instrs[4].id, ur::disassembler::InstructionId::EOR);
    EXPECT_EQ(instrs[4].op_str, "x12, x13, x14");
    ASSERT_EQ(instrs[4].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[4].operands[0].value), ur::assembler::Register::X12);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[4].operands[1].value), ur::assembler::Register::X13);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[4].operands[2].value), ur::assembler::Register::X14);

    // MOV
    EXPECT_EQ(instrs[5].id, ur::disassembler::InstructionId::MOV);
    EXPECT_EQ(instrs[5].op_str, "x15, x16");
    ASSERT_EQ(instrs[5].operands.size(), 2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[5].operands[0].value), ur::assembler::Register::X15);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[5].operands[1].value), ur::assembler::Register::X16);
}

TEST(DisassemblerTest, LoadStorePairInstructions) {
    ur::assembler::Assembler assembler(0x3200);
    assembler.stp(ur::assembler::Register::X0, ur::assembler::Register::X1, ur::assembler::Register::SP, -16, true);
    assembler.ldp(ur::assembler::Register::W2, ur::assembler::Register::W3, ur::assembler::Register::X4, 32);
    assembler.stp(ur::assembler::Register::X5, ur::assembler::Register::X6, ur::assembler::Register::SP, 0);

    const auto& code = assembler.get_code();
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(code.data());
    auto dis = ur::disassembler::CreateAArch64Disassembler();
    auto instrs = dis->Disassemble(0x3200, bytes, code.size() * 4, 3);

    ASSERT_EQ(instrs.size(), 3);
    
    // STP x0, x1, [sp, #-16]!
    EXPECT_EQ(instrs[0].id, ur::disassembler::InstructionId::STP);
    EXPECT_EQ(instrs[0].op_str, "x0, x1, [sp, #-16]!");
    ASSERT_EQ(instrs[0].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[0].operands[0].value), ur::assembler::Register::X0);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[0].operands[1].value), ur::assembler::Register::X1);
    auto& mem0 = std::get<ur::disassembler::MemOperand>(instrs[0].operands[2].value);
    EXPECT_EQ(mem0.base, ur::assembler::Register::SP);
    EXPECT_EQ(mem0.displacement, -16);

    // LDP w2, w3, [x4, #32]
    EXPECT_EQ(instrs[1].id, ur::disassembler::InstructionId::LDP);
    EXPECT_EQ(instrs[1].op_str, "w2, w3, [x4, #32]");
    ASSERT_EQ(instrs[1].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[1].operands[0].value), ur::assembler::Register::W2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[1].operands[1].value), ur::assembler::Register::W3);
    auto& mem1 = std::get<ur::disassembler::MemOperand>(instrs[1].operands[2].value);
    EXPECT_EQ(mem1.base, ur::assembler::Register::X4);
    EXPECT_EQ(mem1.displacement, 32);

    // STP x5, x6, [sp]
    EXPECT_EQ(instrs[2].id, ur::disassembler::InstructionId::STP);
    EXPECT_EQ(instrs[2].op_str, "x5, x6, [sp]");
    ASSERT_EQ(instrs[2].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[2].operands[0].value), ur::assembler::Register::X5);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[2].operands[1].value), ur::assembler::Register::X6);
    auto& mem2 = std::get<ur::disassembler::MemOperand>(instrs[2].operands[2].value);
    EXPECT_EQ(mem2.base, ur::assembler::Register::SP);
    EXPECT_EQ(mem2.displacement, 0);
}

TEST(DisassemblerTest, LoadStoreImmediateInstructions) {
    ur::assembler::Assembler assembler(0x3300);
    assembler.str(ur::assembler::Register::X0, ur::assembler::Register::SP, 8);
    assembler.ldr(ur::assembler::Register::W1, ur::assembler::Register::X2, 12);

    const auto& code = assembler.get_code();
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(code.data());
    auto dis = ur::disassembler::CreateAArch64Disassembler();
    auto instrs = dis->Disassemble(0x3300, bytes, code.size() * 4, 2);

    ASSERT_EQ(instrs.size(), 2);

    // STR x0, [sp, #8]
    EXPECT_EQ(instrs[0].id, ur::disassembler::InstructionId::STR);
    EXPECT_EQ(instrs[0].op_str, "x0, [sp, #8]");
    ASSERT_EQ(instrs[0].operands.size(), 2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[0].operands[0].value), ur::assembler::Register::X0);
    auto& mem0 = std::get<ur::disassembler::MemOperand>(instrs[0].operands[1].value);
    EXPECT_EQ(mem0.base, ur::assembler::Register::SP);
    EXPECT_EQ(mem0.displacement, 8);

    // LDR w1, [x2, #12]
    EXPECT_EQ(instrs[1].id, ur::disassembler::InstructionId::LDR);
    EXPECT_EQ(instrs[1].op_str, "w1, [x2, #12]");
    ASSERT_EQ(instrs[1].operands.size(), 2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[1].operands[0].value), ur::assembler::Register::W1);
    auto& mem1 = std::get<ur::disassembler::MemOperand>(instrs[1].operands[1].value);
    EXPECT_EQ(mem1.base, ur::assembler::Register::X2);
    EXPECT_EQ(mem1.displacement, 12);
}

TEST(DisassemblerTest, PcRelativeInstructions) {
    uint64_t start_addr = 0x3400;
    ur::assembler::Assembler assembler(start_addr);
    uint64_t adr_target = start_addr + 0x10;
    uint64_t adrp_target = 0x4000;
    uint64_t ldr_target = start_addr + 0x20;
    assembler.adr(ur::assembler::Register::X0, adr_target);
    assembler.adrp(ur::assembler::Register::X1, adrp_target);
    assembler.ldr_literal(ur::assembler::Register::X2, ldr_target - (start_addr + 8));

    const auto& code = assembler.get_code();
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(code.data());
    auto dis = ur::disassembler::CreateAArch64Disassembler();
    auto instrs = dis->Disassemble(start_addr, bytes, code.size() * 4, 3);

    ASSERT_EQ(instrs.size(), 3);

    // ADR
    EXPECT_EQ(instrs[0].id, ur::disassembler::InstructionId::ADR);
    EXPECT_TRUE(instrs[0].is_pc_relative);
    ASSERT_EQ(instrs[0].operands.size(), 2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[0].operands[0].value), ur::assembler::Register::X0);
    EXPECT_EQ(std::get<int64_t>(instrs[0].operands[1].value), adr_target);

    // ADRP
    EXPECT_EQ(instrs[1].id, ur::disassembler::InstructionId::ADRP);
    EXPECT_TRUE(instrs[1].is_pc_relative);
    ASSERT_EQ(instrs[1].operands.size(), 2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[1].operands[0].value), ur::assembler::Register::X1);
    EXPECT_EQ(std::get<int64_t>(instrs[1].operands[1].value), adrp_target);

    // LDR literal
    EXPECT_EQ(instrs[2].id, ur::disassembler::InstructionId::LDR_LIT);
    EXPECT_TRUE(instrs[2].is_pc_relative);
    ASSERT_EQ(instrs[2].operands.size(), 2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[2].operands[0].value), ur::assembler::Register::X2);
    EXPECT_EQ(std::get<int64_t>(instrs[2].operands[1].value), ldr_target);
}

TEST(DisassemblerTest, ConditionalBranchingInstructions) {
    uint64_t start_addr = 0x3500;
    ur::assembler::Assembler assembler(start_addr);
    uint64_t b_eq_target = start_addr + 0x28;
    uint64_t cbnz_target = start_addr + 0x30;
    assembler.b(ur::assembler::Condition::EQ, b_eq_target);
    assembler.cbnz(ur::assembler::Register::X0, cbnz_target);

    const auto& code = assembler.get_code();
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(code.data());
    auto dis = ur::disassembler::CreateAArch64Disassembler();
    auto instrs = dis->Disassemble(start_addr, bytes, code.size() * 4, 2);

    ASSERT_EQ(instrs.size(), 2);

    // B.EQ
    EXPECT_EQ(instrs[0].id, ur::disassembler::InstructionId::B_COND);
    EXPECT_TRUE(instrs[0].is_pc_relative);
    EXPECT_EQ(instrs[0].cond, ur::assembler::Condition::EQ);
    ASSERT_EQ(instrs[0].operands.size(), 1);
    EXPECT_EQ(std::get<int64_t>(instrs[0].operands[0].value), b_eq_target);

    // CBNZ
    EXPECT_EQ(instrs[1].id, ur::disassembler::InstructionId::CBNZ);
    EXPECT_TRUE(instrs[1].is_pc_relative);
    ASSERT_EQ(instrs[1].operands.size(), 2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[1].operands[0].value), ur::assembler::Register::X0);
    EXPECT_EQ(std::get<int64_t>(instrs[1].operands[1].value), cbnz_target);
}


TEST(DisassemblerTest, MovInstructions) {
    ur::assembler::Assembler assembler(0x4000);
    assembler.movz(ur::assembler::Register::X0, 0x1234, 16);
    assembler.movk(ur::assembler::Register::W1, 0x5678, 0);

    const auto& assembled_code = assembler.get_code();
    const uint8_t* code_bytes = reinterpret_cast<const uint8_t*>(assembled_code.data());
    size_t code_size = assembled_code.size() * sizeof(uint32_t);

    auto disassembler = ur::disassembler::CreateAArch64Disassembler();
    auto instructions = disassembler->Disassemble(0x4000, code_bytes, code_size, 2);

    ASSERT_EQ(instructions.size(), 2);

    // MOVZ
    EXPECT_EQ(instructions[0].id, ur::disassembler::InstructionId::MOVZ);
    EXPECT_EQ(instructions[0].op_str, "x0, #4660, lsl #16");
    ASSERT_EQ(instructions[0].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[0].operands[0].value), ur::assembler::Register::X0);
    EXPECT_EQ(std::get<int64_t>(instructions[0].operands[1].value), 0x1234);
    EXPECT_EQ(std::get<int64_t>(instructions[0].operands[2].value), 16);

    // MOVK
    EXPECT_EQ(instructions[1].id, ur::disassembler::InstructionId::MOVK);
    EXPECT_EQ(instructions[1].op_str, "w1, #22136");
    ASSERT_EQ(instructions[1].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[1].operands[0].value), ur::assembler::Register::W1);
    EXPECT_EQ(std::get<int64_t>(instructions[1].operands[1].value), 0x5678);
    EXPECT_EQ(std::get<int64_t>(instructions[1].operands[2].value), 0);
}

TEST(DisassemblerTest, EnhancedInstructions) {
    uint64_t start_addr = 0x6000;
    // Pre-calculated instruction words
    uint32_t code[] = {
        0x92800021, // movn x1, #1, lsl #0
        0xF8627843, // ldr x3, [x2, x2]
        0x37240084, // tbnz x4, #4, 0x6018 (offset 16 bytes from this instruction, which is at 0x6008)
        0x12003C65, // and w5, w3, #0xff
    };
    const uint8_t* code_bytes = reinterpret_cast<const uint8_t*>(code);
    size_t code_size = sizeof(code);

    auto disassembler = ur::disassembler::CreateAArch64Disassembler();
    auto instructions = disassembler->Disassemble(start_addr, code_bytes, code_size, 4);

    ASSERT_EQ(instructions.size(), 4);

    // MOVN
    EXPECT_EQ(instructions[0].id, ur::disassembler::InstructionId::MOVN);
    EXPECT_EQ(instructions[0].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instructions[0].mnemonic, "movn");
    EXPECT_FALSE(instructions[0].is_pc_relative);
    ASSERT_EQ(instructions[0].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[0].operands[0].value), ur::assembler::Register::X1);
    EXPECT_EQ(std::get<int64_t>(instructions[0].operands[1].value), 1);

    // LDR (register)
    EXPECT_EQ(instructions[1].id, ur::disassembler::InstructionId::LDR);
    EXPECT_EQ(instructions[1].group, ur::disassembler::InstructionGroup::LOAD_STORE);
    EXPECT_FALSE(instructions[1].is_pc_relative);
    EXPECT_EQ(instructions[1].mnemonic, "ldr");
    ASSERT_EQ(instructions[1].operands.size(), 2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[1].operands[0].value), ur::assembler::Register::X3);

    // TBNZ
    EXPECT_EQ(instructions[2].id, ur::disassembler::InstructionId::TBNZ);
    EXPECT_EQ(instructions[2].group, ur::disassembler::InstructionGroup::JUMP);
    EXPECT_TRUE(instructions[2].is_pc_relative);
    int64_t tbnz_offset = -32752; // Calculated from the instruction word 0x37240084
    uint64_t tbnz_target = start_addr + 8 + tbnz_offset;
    EXPECT_EQ(instructions[2].op_str, "x4, #4, " + format_address(tbnz_target));
    ASSERT_EQ(instructions[2].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[2].operands[0].value), ur::assembler::Register::X4);
    EXPECT_EQ(std::get<int64_t>(instructions[2].operands[1].value), 4);
    EXPECT_EQ(std::get<int64_t>(instructions[2].operands[2].value), static_cast<int64_t>(tbnz_target));

    // AND (immediate)
    EXPECT_EQ(instructions[3].id, ur::disassembler::InstructionId::AND);
    EXPECT_EQ(instructions[3].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_FALSE(instructions[3].is_pc_relative);
    EXPECT_EQ(instructions[3].mnemonic, "and");
    ASSERT_EQ(instructions[3].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[3].operands[0].value), ur::assembler::Register::W5);
    EXPECT_EQ(std::get<ur::assembler::Register>(instructions[3].operands[1].value), ur::assembler::Register::W3);
    EXPECT_EQ(instructions[3].operands[2].type, ur::disassembler::OperandType::IMMEDIATE);
}

TEST(DisassemblerTest, UnknownInstruction) {
    uint32_t unknown_instr_word = 0x00000000; // Real "unknown" instruction
    const uint8_t* code_bytes = reinterpret_cast<const uint8_t*>(&unknown_instr_word);
    size_t code_size = sizeof(uint32_t);

    auto disassembler = ur::disassembler::CreateAArch64Disassembler();
    auto instructions = disassembler->Disassemble(0x5000, code_bytes, code_size, 1);

    ASSERT_EQ(instructions.size(), 1);
    EXPECT_EQ(instructions[0].id, ur::disassembler::InstructionId::INVALID);
    EXPECT_EQ(instructions[0].group, ur::disassembler::InstructionGroup::INVALID);
    EXPECT_EQ(instructions[0].mnemonic, "unknown");
    EXPECT_EQ(instructions[0].op_str, "0x0");
    EXPECT_TRUE(instructions[0].operands.empty());
}

TEST(DisassemblerTest, FloatAndExclusiveInstructions) {
    uint64_t start_addr = 0x7000;
    ur::assembler::Assembler assembler(start_addr);

    assembler.fadd(ur::assembler::Register::D0, ur::assembler::Register::D1, ur::assembler::Register::D2);
    assembler.fmov(ur::assembler::Register::S3, ur::assembler::Register::S4);
    assembler.scvtf(ur::assembler::Register::D5, ur::assembler::Register::X6);
    assembler.fcvtzs(ur::assembler::Register::W7, ur::assembler::Register::S8);
    assembler.ldxr(ur::assembler::Register::X9, ur::assembler::Register::X10);
    assembler.stxr(ur::assembler::Register::W11, ur::assembler::Register::X12, ur::assembler::Register::X13);
    assembler.lsl(ur::assembler::Register::X14, ur::assembler::Register::X15, 1);


    const auto& code = assembler.get_code();
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(code.data());
    auto dis = ur::disassembler::CreateAArch64Disassembler();
    auto instrs = dis->Disassemble(start_addr, bytes, code.size() * 4, 7);

    ASSERT_EQ(instrs.size(), 7);

    // FADD
    EXPECT_EQ(instrs[0].id, ur::disassembler::InstructionId::FADD);
    EXPECT_EQ(instrs[0].group, ur::disassembler::InstructionGroup::FLOAT_SIMD);
    EXPECT_EQ(instrs[0].op_str, "d0, d1, d2");
    ASSERT_EQ(instrs[0].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[0].operands[0].value), ur::assembler::Register::D0);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[0].operands[1].value), ur::assembler::Register::D1);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[0].operands[2].value), ur::assembler::Register::D2);

    // FMOV
    EXPECT_EQ(instrs[1].id, ur::disassembler::InstructionId::FMOV);
    EXPECT_EQ(instrs[1].group, ur::disassembler::InstructionGroup::FLOAT_SIMD);
    EXPECT_EQ(instrs[1].op_str, "s3, s4");
    ASSERT_EQ(instrs[1].operands.size(), 2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[1].operands[0].value), ur::assembler::Register::S3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[1].operands[1].value), ur::assembler::Register::S4);

    // SCVTF
    EXPECT_EQ(instrs[2].id, ur::disassembler::InstructionId::SCVTF);
    EXPECT_EQ(instrs[2].group, ur::disassembler::InstructionGroup::FLOAT_SIMD);
    EXPECT_EQ(instrs[2].op_str, "d5, x6");
    ASSERT_EQ(instrs[2].operands.size(), 2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[2].operands[0].value), ur::assembler::Register::D5);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[2].operands[1].value), ur::assembler::Register::X6);

    // FCVTZS
    EXPECT_EQ(instrs[3].id, ur::disassembler::InstructionId::FCVTZS);
    EXPECT_EQ(instrs[3].group, ur::disassembler::InstructionGroup::FLOAT_SIMD);
    EXPECT_EQ(instrs[3].op_str, "w7, s8");
    ASSERT_EQ(instrs[3].operands.size(), 2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[3].operands[0].value), ur::assembler::Register::W7);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[3].operands[1].value), ur::assembler::Register::S8);

    // LDXR
    EXPECT_EQ(instrs[4].id, ur::disassembler::InstructionId::LDXR);
    EXPECT_EQ(instrs[4].group, ur::disassembler::InstructionGroup::LOAD_STORE);
    EXPECT_EQ(instrs[4].op_str, "x9, [x10]");
    ASSERT_EQ(instrs[4].operands.size(), 2);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[4].operands[0].value), ur::assembler::Register::X9);

    // STXR
    EXPECT_EQ(instrs[5].id, ur::disassembler::InstructionId::STXR);
    EXPECT_EQ(instrs[5].group, ur::disassembler::InstructionGroup::LOAD_STORE);
    EXPECT_EQ(instrs[5].op_str, "w11, x12, [x13]");
    ASSERT_EQ(instrs[5].operands.size(), 3);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[5].operands[0].value), ur::assembler::Register::W11);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[5].operands[1].value), ur::assembler::Register::X12);

    // UBFM (LSL)
    EXPECT_EQ(instrs[6].id, ur::disassembler::InstructionId::UBFM);
    EXPECT_EQ(instrs[6].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instrs[6].mnemonic, "ubfm");
    ASSERT_EQ(instrs[6].operands.size(), 4);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[6].operands[0].value), ur::assembler::Register::X14);
    EXPECT_EQ(std::get<ur::assembler::Register>(instrs[6].operands[1].value), ur::assembler::Register::X15);
}
