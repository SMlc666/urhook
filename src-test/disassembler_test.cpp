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

    EXPECT_EQ(instructions[0].id, ur::disassembler::InstructionId::B);
    EXPECT_EQ(instructions[0].group, ur::disassembler::InstructionGroup::JUMP);
    EXPECT_EQ(instructions[0].mnemonic, "b");
    EXPECT_EQ(instructions[0].op_str, format_address(target_b));

    EXPECT_EQ(instructions[1].id, ur::disassembler::InstructionId::BL);
    EXPECT_EQ(instructions[1].group, ur::disassembler::InstructionGroup::JUMP);
    EXPECT_EQ(instructions[1].mnemonic, "bl");
    EXPECT_EQ(instructions[1].op_str, format_address(target_bl));

    EXPECT_EQ(instructions[2].id, ur::disassembler::InstructionId::BR);
    EXPECT_EQ(instructions[2].group, ur::disassembler::InstructionGroup::JUMP);
    EXPECT_EQ(instructions[2].mnemonic, "br");
    EXPECT_EQ(instructions[2].op_str, "x10");

    EXPECT_EQ(instructions[3].id, ur::disassembler::InstructionId::BLR);
    EXPECT_EQ(instructions[3].group, ur::disassembler::InstructionGroup::JUMP);
    EXPECT_EQ(instructions[3].mnemonic, "blr");
    EXPECT_EQ(instructions[3].op_str, "x11");
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

    EXPECT_EQ(instructions[0].id, ur::disassembler::InstructionId::ADD);
    EXPECT_EQ(instructions[0].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instructions[0].mnemonic, "add");
    EXPECT_EQ(instructions[0].op_str, "x0, x1, #16");

    EXPECT_EQ(instructions[1].id, ur::disassembler::InstructionId::SUB);
    EXPECT_EQ(instructions[1].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instructions[1].mnemonic, "sub");
    EXPECT_EQ(instructions[1].op_str, "w2, w3, #32, lsl #12");

    EXPECT_EQ(instructions[2].id, ur::disassembler::InstructionId::ADD);
    EXPECT_EQ(instructions[2].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instructions[2].mnemonic, "add");
    EXPECT_EQ(instructions[2].op_str, "sp, sp, #16");
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
    EXPECT_EQ(instrs[0].id, ur::disassembler::InstructionId::ADD);
    EXPECT_EQ(instrs[0].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instrs[0].mnemonic, "add");
    EXPECT_EQ(instrs[0].op_str, "x0, x1, x2");

    EXPECT_EQ(instrs[1].id, ur::disassembler::InstructionId::SUB);
    EXPECT_EQ(instrs[1].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instrs[1].mnemonic, "sub");
    EXPECT_EQ(instrs[1].op_str, "w3, w4, w5");

    EXPECT_EQ(instrs[2].id, ur::disassembler::InstructionId::AND);
    EXPECT_EQ(instrs[2].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instrs[2].mnemonic, "and");
    EXPECT_EQ(instrs[2].op_str, "x6, x7, x8");

    EXPECT_EQ(instrs[3].id, ur::disassembler::InstructionId::ORR);
    EXPECT_EQ(instrs[3].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instrs[3].mnemonic, "orr");
    EXPECT_EQ(instrs[3].op_str, "w9, w10, w11");

    EXPECT_EQ(instrs[4].id, ur::disassembler::InstructionId::EOR);
    EXPECT_EQ(instrs[4].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instrs[4].mnemonic, "eor");
    EXPECT_EQ(instrs[4].op_str, "x12, x13, x14");

    EXPECT_EQ(instrs[5].id, ur::disassembler::InstructionId::MOV);
    EXPECT_EQ(instrs[5].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instrs[5].mnemonic, "mov");
    EXPECT_EQ(instrs[5].op_str, "x15, x16");
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
    EXPECT_EQ(instrs[0].id, ur::disassembler::InstructionId::STP);
    EXPECT_EQ(instrs[0].group, ur::disassembler::InstructionGroup::LOAD_STORE);
    EXPECT_EQ(instrs[0].mnemonic, "stp");
    EXPECT_EQ(instrs[0].op_str, "x0, x1, [sp, #-16]!");

    EXPECT_EQ(instrs[1].id, ur::disassembler::InstructionId::LDP);
    EXPECT_EQ(instrs[1].group, ur::disassembler::InstructionGroup::LOAD_STORE);
    EXPECT_EQ(instrs[1].mnemonic, "ldp");
    EXPECT_EQ(instrs[1].op_str, "w2, w3, [x4, #32]");

    EXPECT_EQ(instrs[2].id, ur::disassembler::InstructionId::STP);
    EXPECT_EQ(instrs[2].group, ur::disassembler::InstructionGroup::LOAD_STORE);
    EXPECT_EQ(instrs[2].mnemonic, "stp");
    EXPECT_EQ(instrs[2].op_str, "x5, x6, [sp]");
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
    EXPECT_EQ(instrs[0].id, ur::disassembler::InstructionId::STR);
    EXPECT_EQ(instrs[0].group, ur::disassembler::InstructionGroup::LOAD_STORE);
    EXPECT_EQ(instrs[0].mnemonic, "str");
    EXPECT_EQ(instrs[0].op_str, "x0, [sp, #8]");

    EXPECT_EQ(instrs[1].id, ur::disassembler::InstructionId::LDR);
    EXPECT_EQ(instrs[1].group, ur::disassembler::InstructionGroup::LOAD_STORE);
    EXPECT_EQ(instrs[1].mnemonic, "ldr");
    EXPECT_EQ(instrs[1].op_str, "w1, [x2, #12]");
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
    EXPECT_EQ(instrs[0].id, ur::disassembler::InstructionId::ADR);
    EXPECT_EQ(instrs[0].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instrs[0].mnemonic, "adr");
    EXPECT_EQ(instrs[0].op_str, "x0, " + format_address(adr_target));
    EXPECT_TRUE(instrs[0].is_pc_relative);

    EXPECT_EQ(instrs[1].id, ur::disassembler::InstructionId::ADRP);
    EXPECT_EQ(instrs[1].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instrs[1].mnemonic, "adrp");
    EXPECT_EQ(instrs[1].op_str, "x1, " + format_address(adrp_target));
    EXPECT_TRUE(instrs[1].is_pc_relative);

    EXPECT_EQ(instrs[2].id, ur::disassembler::InstructionId::LDR_LIT);
    EXPECT_EQ(instrs[2].group, ur::disassembler::InstructionGroup::LOAD_STORE);
    EXPECT_EQ(instrs[2].mnemonic, "ldr");
    EXPECT_EQ(instrs[2].op_str, "x2, " + format_address(ldr_target));
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
    EXPECT_EQ(instrs[0].id, ur::disassembler::InstructionId::B_COND);
    EXPECT_EQ(instrs[0].group, ur::disassembler::InstructionGroup::JUMP);
    EXPECT_EQ(instrs[0].mnemonic, "b.eq");
    EXPECT_EQ(instrs[0].op_str, format_address(b_eq_target));

    EXPECT_EQ(instrs[1].id, ur::disassembler::InstructionId::CBNZ);
    EXPECT_EQ(instrs[1].group, ur::disassembler::InstructionGroup::JUMP);
    EXPECT_EQ(instrs[1].mnemonic, "cbnz");
    EXPECT_EQ(instrs[1].op_str, "x0, " + format_address(cbnz_target));
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

    EXPECT_EQ(instructions[0].id, ur::disassembler::InstructionId::MOVZ);
    EXPECT_EQ(instructions[0].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instructions[0].mnemonic, "movz");
    EXPECT_EQ(instructions[0].op_str, "x0, #4660, lsl #16");

    EXPECT_EQ(instructions[1].id, ur::disassembler::InstructionId::MOVK);
    EXPECT_EQ(instructions[1].group, ur::disassembler::InstructionGroup::DATA_PROCESSING);
    EXPECT_EQ(instructions[1].mnemonic, "movk");
    EXPECT_EQ(instructions[1].op_str, "w1, #22136");
}

TEST(DisassemblerTest, UnknownInstruction) {
    uint32_t unknown_instr_word = 0x12345678;
    const uint8_t* code_bytes = reinterpret_cast<const uint8_t*>(&unknown_instr_word);
    size_t code_size = sizeof(uint32_t);

    auto disassembler = ur::disassembler::CreateAArch64Disassembler();
    auto instructions = disassembler->Disassemble(0x5000, code_bytes, code_size, 1);

    ASSERT_EQ(instructions.size(), 1);
    EXPECT_EQ(instructions[0].id, ur::disassembler::InstructionId::INVALID);
    EXPECT_EQ(instructions[0].group, ur::disassembler::InstructionGroup::INVALID);
    EXPECT_EQ(instructions[0].mnemonic, "unknown");
    EXPECT_EQ(instructions[0].op_str, "0x12345678");
}