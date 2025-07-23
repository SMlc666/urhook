#include "ur/disassembler.h"
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>

namespace ur {
    namespace disassembler {

        namespace {
            // Helper to create an operand
            Operand create_reg_operand(assembler::Register reg) {
                return {OperandType::REGISTER, reg};
            }

            Operand create_imm_operand(int64_t imm) {
                return {OperandType::IMMEDIATE, imm};
            }

            Operand create_mem_operand(assembler::Register base, int32_t disp) {
                return {OperandType::MEMORY, MemOperand{base, assembler::Register::INVALID, disp}};
            }

            // Helper to get general-purpose register name string
            static std::string get_reg_name(uint32_t reg_num, bool is_64bit, bool is_sp_context = false) {
                if (reg_num == 31) {
                    if (is_sp_context) {
                        return is_64bit ? "sp" : "wsp";
                    }
                    return is_64bit ? "zr" : "wzr";
                }
                return (is_64bit ? "x" : "w") + std::to_string(reg_num);
            }
            
            static assembler::Register get_reg_enum(uint32_t reg_num, bool is_64bit) {
                 if (is_64bit) {
                    return static_cast<assembler::Register>(static_cast<int>(assembler::Register::X0) + reg_num);
                } else {
                    return static_cast<assembler::Register>(static_cast<int>(assembler::Register::W0) + reg_num);
                }
            }

            // Helper to get condition code string
            static std::string get_cond_name(uint32_t cond) {
                static const char* cond_names[] = {
                    "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
                    "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"
                };
                if (cond < 16) {
                    return cond_names[cond];
                }
                return "invalid";
            }

            // Helper to get floating-point/SIMD register name string
            static std::string get_fp_reg_name(uint32_t reg_num, uint32_t type) {
                // type: 00=S, 01=D, 10=Q
                std::string prefix;
                switch (type) {
                    case 0: prefix = "s"; break;
                    case 1: prefix = "d"; break;
                    case 2: prefix = "q"; break;
                    default: return "invalid";
                }
                return prefix + std::to_string(reg_num);
            }

            static assembler::Register get_fp_reg_enum(uint32_t reg_num, uint32_t type) {
                switch (type) {
                    case 0: return static_cast<assembler::Register>(static_cast<int>(assembler::Register::S0) + reg_num);
                    case 1: return static_cast<assembler::Register>(static_cast<int>(assembler::Register::D0) + reg_num);
                    case 2: return static_cast<assembler::Register>(static_cast<int>(assembler::Register::Q0) + reg_num);
                    default: return assembler::Register::INVALID;
                }
            }
        } // namespace

        class AArch64Disassembler : public Disassembler {
        public:
            std::vector<Instruction> Disassemble(
                uint64_t address,
                const uint8_t* code,
                size_t code_size,
                size_t count
            ) override {
                std::vector<Instruction> instructions;
                uint64_t current_address = address;
                size_t bytes_disassembled = 0;

                for (size_t i = 0; i < count && bytes_disassembled + 4 <= code_size; ++i) {
                    Instruction instr;
                    instr.address = current_address;
                    instr.size = 4;
                    instr.bytes.assign(code + bytes_disassembled, code + bytes_disassembled + 4);

                    uint32_t instr_word = *reinterpret_cast<const uint32_t*>(code + bytes_disassembled);

                    decode_instruction(instr, instr_word);

                    instructions.push_back(instr);

                    current_address += 4;
                    bytes_disassembled += 4;
                }
                return instructions;
            }

        private:
            void decode_instruction(Instruction& instr, uint32_t instr_word) {
                // NOP
                if (instr_word == 0xD503201F) {
                    instr.id = InstructionId::NOP;
                    instr.group = InstructionGroup::SYSTEM;
                    instr.mnemonic = "nop";
                    return;
                }
                // RET
                if (instr_word == 0xD65F03C0) {
                    instr.id = InstructionId::RET;
                    instr.group = InstructionGroup::JUMP;
                    instr.mnemonic = "ret";
                    return;
                }

                // Unconditional branch (immediate)
                if ((instr_word & 0xFC000000) == 0x14000000) {
                    instr.id = InstructionId::B;
                    instr.group = InstructionGroup::JUMP;
                    instr.mnemonic = "b";
                    instr.is_pc_relative = true;
                    int64_t imm26 = instr_word & 0x03FFFFFF;
                    if (imm26 & 0x02000000) imm26 |= ~0x03FFFFFFLL; // Sign extend
                    int64_t offset = imm26 * 4;
                    uint64_t target = instr.address + offset;
                    instr.operands.push_back(create_imm_operand(target));
                    std::stringstream ss;
                    ss << "0x" << std::hex << target;
                    instr.op_str = ss.str();
                    return;
                }
                
                // Branch with link (immediate)
                if ((instr_word & 0xFC000000) == 0x94000000) {
                    instr.id = InstructionId::BL;
                    instr.group = InstructionGroup::JUMP;
                    instr.mnemonic = "bl";
                    instr.is_pc_relative = true;
                    int64_t imm26 = instr_word & 0x03FFFFFF;
                    if (imm26 & 0x02000000) imm26 |= ~0x03FFFFFFLL; // Sign extend
                    int64_t offset = imm26 * 4;
                    uint64_t target = instr.address + offset;
                    instr.operands.push_back(create_imm_operand(target));
                    std::stringstream ss;
                    ss << "0x" << std::hex << target;
                    instr.op_str = ss.str();
                    return;
                }

                // Branch register
                if ((instr_word & 0xFFFFFC1F) == 0xD61F0000) { // BR
                    instr.id = InstructionId::BR;
                    instr.group = InstructionGroup::JUMP;
                    instr.mnemonic = "br";
                    uint32_t rn = (instr_word >> 5) & 0x1F;
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rn, true)));
                    instr.op_str = get_reg_name(rn, true);
                    return;
                }

                // Branch with link register
                if ((instr_word & 0xFFFFFC1F) == 0xD63F0000) { // BLR
                    instr.id = InstructionId::BLR;
                    instr.group = InstructionGroup::JUMP;
                    instr.mnemonic = "blr";
                    uint32_t rn = (instr_word >> 5) & 0x1F;
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rn, true)));
                    instr.op_str = get_reg_name(rn, true);
                    return;
                }

                // Conditional branch
                if ((instr_word & 0xFE000000) == 0x54000000) {
                    instr.id = InstructionId::B_COND;
                    instr.group = InstructionGroup::JUMP;
                    instr.is_pc_relative = true;
                    uint32_t cond_val = instr_word & 0xF;
                    instr.cond = static_cast<assembler::Condition>(cond_val);
                    instr.mnemonic = "b." + get_cond_name(cond_val);
                    int64_t imm19 = (instr_word >> 5) & 0x7FFFF;
                    if (imm19 & 0x40000) imm19 |= ~0x7FFFFLL; // Sign extend
                    int64_t offset = imm19 * 4;
                    uint64_t target = instr.address + offset;
                    instr.operands.push_back(create_imm_operand(target));
                    std::stringstream ss;
                    ss << "0x" << std::hex << target;
                    instr.op_str = ss.str();
                    return;
                }

                // Compare and branch (zero/non-zero)
                if ((instr_word & 0x7E000000) == 0x34000000) {
                    instr.group = InstructionGroup::JUMP;
                    instr.is_pc_relative = true;
                    bool sf = (instr_word >> 31) & 1;
                    bool op = (instr_word >> 24) & 1;
                    instr.id = op ? InstructionId::CBNZ : InstructionId::CBZ;
                    instr.mnemonic = op ? "cbnz" : "cbz";
                    uint32_t rt = instr_word & 0x1F;
                    int64_t imm19 = (instr_word >> 5) & 0x7FFFF;
                    if (imm19 & 0x40000) imm19 |= ~0x7FFFFLL; // Sign extend for negative offsets
                    int64_t offset = imm19 * 4;
                    uint64_t target = instr.address + offset;
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rt, sf)));
                    instr.operands.push_back(create_imm_operand(target));
                    std::stringstream ss;
                    ss << get_reg_name(rt, sf) << ", 0x" << std::hex << target;
                    instr.op_str = ss.str();
                    return;
                }

                // ADD/SUB (immediate)
                if ((instr_word & 0x1F000000) == 0x11000000) {
                    instr.group = InstructionGroup::DATA_PROCESSING;
                    bool sf = (instr_word >> 31) & 1;
                    bool op = (instr_word >> 30) & 1;
                    bool s = (instr_word >> 29) & 1;
                    if (op) {
                        instr.id = s ? InstructionId::SUBS : InstructionId::SUB;
                        instr.mnemonic = s ? "subs" : "sub";
                    } else {
                        instr.id = s ? InstructionId::ADDS : InstructionId::ADD;
                        instr.mnemonic = s ? "adds" : "add";
                    }
                    
                    uint32_t rd = instr_word & 0x1F;
                    uint32_t rn = (instr_word >> 5) & 0x1F;
                    uint32_t imm12 = (instr_word >> 10) & 0xFFF;
                    bool shift = (instr_word >> 22) & 1;
                    uint32_t final_imm = shift ? (imm12 << 12) : imm12;

                    instr.operands.push_back(create_reg_operand(get_reg_enum(rd, sf)));
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rn, sf)));
                    instr.operands.push_back(create_imm_operand(final_imm));

                    std::stringstream ss;
                    ss << get_reg_name(rd, sf, rd == 31) << ", " << get_reg_name(rn, sf, rn == 31) << ", #" << imm12;
                    if (shift) {
                        ss << ", lsl #12";
                    }
                    instr.op_str = ss.str();
                    return;
                }

                // Logical (shifted register) - covers AND, ORR, EOR, ANDS
                if ((instr_word & 0x1F800000) == 0x0A000000) {
                    instr.group = InstructionGroup::DATA_PROCESSING;
                    bool sf = (instr_word >> 31) & 1;
                    uint32_t opc = (instr_word >> 29) & 0x3;
                    uint32_t n = (instr_word >> 21) & 1;
                    uint32_t rd = instr_word & 0x1F;
                    uint32_t rn = (instr_word >> 5) & 0x1F;
                    uint32_t rm = (instr_word >> 16) & 0x1F;

                    if (opc == 1 && rn == 31 && n == 0) { // ORR with ZR is MOV
                        instr.id = InstructionId::MOV;
                        instr.mnemonic = "mov";
                        instr.operands.push_back(create_reg_operand(get_reg_enum(rd, sf)));
                        instr.operands.push_back(create_reg_operand(get_reg_enum(rm, sf)));
                        instr.op_str = get_reg_name(rd, sf) + ", " + get_reg_name(rm, sf);
                    } else {
                        switch(opc) {
                            case 0: instr.id = InstructionId::AND; instr.mnemonic = "and"; break;
                            case 1: instr.id = InstructionId::ORR; instr.mnemonic = "orr"; break;
                            case 2: instr.id = InstructionId::EOR; instr.mnemonic = "eor"; break;
                            case 3: instr.id = InstructionId::ANDS; instr.mnemonic = "ands"; break;
                        }
                        instr.operands.push_back(create_reg_operand(get_reg_enum(rd, sf)));
                        instr.operands.push_back(create_reg_operand(get_reg_enum(rn, sf)));
                        instr.operands.push_back(create_reg_operand(get_reg_enum(rm, sf)));
                        std::stringstream ss;
                        ss << get_reg_name(rd, sf) << ", " << get_reg_name(rn, sf) << ", " << get_reg_name(rm, sf);
                        instr.op_str = ss.str();
                    }
                    return;
                }

                // ADD/SUB (shifted register)
                if ((instr_word & 0x1F200000) == 0x0B000000) {
                    instr.group = InstructionGroup::DATA_PROCESSING;
                    bool sf = (instr_word >> 31) & 1;
                    bool op = (instr_word >> 30) & 1;
                    bool s = (instr_word >> 29) & 1;
                    if (op) {
                        instr.id = s ? InstructionId::SUBS : InstructionId::SUB;
                        instr.mnemonic = s ? "subs" : "sub";
                    } else {
                        instr.id = s ? InstructionId::ADDS : InstructionId::ADD;
                        instr.mnemonic = s ? "adds" : "add";
                    }
                    uint32_t rd = instr_word & 0x1F;
                    uint32_t rn = (instr_word >> 5) & 0x1F;
                    uint32_t rm = (instr_word >> 16) & 0x1F;
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rd, sf)));
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rn, sf)));
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rm, sf)));
                    std::stringstream ss;
                    ss << get_reg_name(rd, sf, rd == 31) << ", " << get_reg_name(rn, sf, rn == 31) << ", " << get_reg_name(rm, sf);
                    instr.op_str = ss.str();
                    return;
                }

                // ADR
                if ((instr_word & 0x9F000000) == 0x10000000) {
                    instr.id = InstructionId::ADR;
                    instr.group = InstructionGroup::DATA_PROCESSING;
                    instr.mnemonic = "adr";
                    instr.is_pc_relative = true;
                    uint32_t rd = instr_word & 0x1F;
                    int64_t immhi = (instr_word >> 5) & 0x7FFFF;
                    int64_t immlo = (instr_word >> 29) & 0x3;
                    int64_t imm = (immhi << 2) | immlo;
                    if (imm & (1LL << 20)) imm |= ~((1LL << 21) - 1); // Sign extend 21-bit
                    uint64_t target = instr.address + imm;
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rd, true)));
                    instr.operands.push_back(create_imm_operand(target));
                    std::stringstream ss;
                    ss << get_reg_name(rd, true) << ", 0x" << std::hex << target;
                    instr.op_str = ss.str();
                    return;
                }

                // ADRP
                if ((instr_word & 0x9F000000) == 0x90000000) {
                    instr.id = InstructionId::ADRP;
                    instr.group = InstructionGroup::DATA_PROCESSING;
                    instr.mnemonic = "adrp";
                    instr.is_pc_relative = true;
                    uint32_t rd = instr_word & 0x1F;
                    int64_t immhi = (instr_word >> 5) & 0x7FFFF;
                    int64_t immlo = (instr_word >> 29) & 0x3;
                    int64_t imm = (immhi << 2) | immlo;
                    if (imm & (1LL << 20)) imm |= ~((1LL << 21) - 1); // Sign extend 21-bit
                    uint64_t target = (instr.address & ~0xFFFULL) + (imm << 12);
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rd, true)));
                    instr.operands.push_back(create_imm_operand(target));
                    std::stringstream ss;
                    ss << get_reg_name(rd, true) << ", 0x" << std::hex << target;
                    instr.op_str = ss.str();
                    return;
                }

                // LDR/STR (immediate, unsigned offset)
                if ((instr_word & 0x3B000000) == 0x39000000) {
                    instr.group = InstructionGroup::LOAD_STORE;
                    uint32_t size = (instr_word >> 30) & 0x3;
                    bool is_load = (instr_word >> 22) & 1;
                    instr.id = is_load ? InstructionId::LDR : InstructionId::STR;
                    instr.mnemonic = is_load ? "ldr" : "str";
                    uint32_t rt = instr_word & 0x1F;
                    uint32_t rn = (instr_word >> 5) & 0x1F;
                    uint32_t imm12 = (instr_word >> 10) & 0xFFF;
                    uint32_t offset = imm12 << size;
                    bool is_64bit_reg = size >= 3;
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rt, is_64bit_reg)));
                    instr.operands.push_back(create_mem_operand(get_reg_enum(rn, true), offset));
                    std::stringstream ss;
                    ss << get_reg_name(rt, is_64bit_reg) << ", [" << get_reg_name(rn, true, true) << ", #" << offset << "]";
                    instr.op_str = ss.str();
                    return;
                }

                // LDP/STP
                if ((instr_word & 0x3E000000) == 0x28000000) {
                    instr.group = InstructionGroup::LOAD_STORE;
                    uint32_t opc = (instr_word >> 30) & 0x3;
                    bool is_64bit = (opc == 2);
                    bool is_load = (instr_word >> 22) & 1;
                    instr.id = is_load ? InstructionId::LDP : InstructionId::STP;
                    instr.mnemonic = is_load ? "ldp" : "stp";
                    uint32_t rt1 = instr_word & 0x1F;
                    uint32_t rn = (instr_word >> 5) & 0x1F;
                    uint32_t rt2 = (instr_word >> 10) & 0x1F;
                    int32_t imm7 = (instr_word >> 15) & 0x7F;
                    if (imm7 & 0x40) imm7 |= ~0x7F; // Sign extend
                    uint32_t p_w_bits = (instr_word >> 23) & 0x3;
                    int scale = is_64bit ? 3 : 2;
                    int32_t offset = imm7 << scale;
                    
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rt1, is_64bit)));
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rt2, is_64bit)));
                    instr.operands.push_back(create_mem_operand(get_reg_enum(rn, true), offset));

                    std::stringstream ss;
                    ss << get_reg_name(rt1, is_64bit) << ", " << get_reg_name(rt2, is_64bit) << ", [" << get_reg_name(rn, true, true);
                    if (p_w_bits == 0b10) { // signed offset
                        if (offset != 0) ss << ", #" << offset;
                        ss << "]";
                    } else if (p_w_bits == 0b11) { // pre-index
                        ss << ", #" << offset << "]!";
                    } else if (p_w_bits == 0b01) { // post-index
                        ss << "], #" << offset;
                    }
                    instr.op_str = ss.str();
                    return;
                }

                // LDR (literal)
                if ((instr_word & 0x3F000000) == 0x18000000) {
                    instr.id = InstructionId::LDR_LIT;
                    instr.group = InstructionGroup::LOAD_STORE;
                    instr.is_pc_relative = true;
                    bool is_64bit = (instr_word >> 30) & 1;
                    instr.mnemonic = "ldr";
                    uint32_t rt = instr_word & 0x1F;
                    int64_t imm19 = (instr_word >> 5) & 0x7FFFF;
                    if (imm19 & 0x40000) imm19 |= ~0x7FFFFLL; // Sign extend
                    int64_t offset = imm19 * 4;
                    uint64_t target = instr.address + offset;
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rt, is_64bit)));
                    instr.operands.push_back(create_imm_operand(target));
                    std::stringstream ss;
                    ss << get_reg_name(rt, is_64bit) << ", 0x" << std::hex << target;
                    instr.op_str = ss.str();
                    return;
                }

                // LDR/STR (register offset)
                if ((instr_word & 0x3B200800) == 0x38200800) {
                    instr.group = InstructionGroup::LOAD_STORE;
                    uint32_t size = (instr_word >> 30) & 0x3;
                    bool is_load = (instr_word >> 22) & 1;
                    instr.id = is_load ? InstructionId::LDR : InstructionId::STR;
                    instr.mnemonic = is_load ? "ldr" : "str";
                    uint32_t rt = instr_word & 0x1F;
                    uint32_t rn = (instr_word >> 5) & 0x1F;
                    uint32_t rm = (instr_word >> 16) & 0x1F;
                    bool is_64bit_reg = size >= 3;

                    instr.operands.push_back(create_reg_operand(get_reg_enum(rt, is_64bit_reg)));
                    instr.operands.push_back(create_mem_operand(get_reg_enum(rn, true), 0)); // Placeholder, complex mem operand
                    
                    std::stringstream ss;
                    ss << get_reg_name(rt, is_64bit_reg) << ", [" << get_reg_name(rn, true, true) << ", " << get_reg_name(rm, true) << "]";
                    instr.op_str = ss.str();
                    return;
                }

                // Test and branch (zero/non-zero)
                if ((instr_word & 0x7E000000) == 0x36000000) {
                    instr.group = InstructionGroup::JUMP;
                    instr.is_pc_relative = true;
                    bool op = (instr_word >> 24) & 1;
                    instr.id = op ? InstructionId::TBNZ : InstructionId::TBZ;
                    instr.mnemonic = op ? "tbnz" : "tbz";
                    uint32_t rt = instr_word & 0x1F;
                    uint32_t b5 = (instr_word >> 31) & 1;
                    uint32_t b40 = (instr_word >> 19) & 0x1F;
                    uint32_t bit_pos = (b5 << 5) | b40;
                    int64_t imm14 = (instr_word >> 5) & 0x3FFF;
                    if (imm14 & 0x2000) imm14 |= ~0x3FFFLL; // Sign extend
                    int64_t offset = imm14 * 4;
                    uint64_t target = instr.address + offset;
                    
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rt, true)));
                    instr.operands.push_back(create_imm_operand(bit_pos));
                    instr.operands.push_back(create_imm_operand(target));

                    std::stringstream ss;
                    ss << get_reg_name(rt, true) << ", #" << bit_pos << ", 0x" << std::hex << target;
                    instr.op_str = ss.str();
                    return;
                }

                // Logical (immediate)
                if ((instr_word & 0x1F800000) == 0x12000000) {
                    // This is a very simplified check. A real implementation needs to decode N, immr, imms.
                    // For now, we just want to distinguish it from other instructions.
                    if (((instr_word >> 23) & 0x7) != 0) { // Check for non-zero N, immr, imms fields for common forms
                        instr.group = InstructionGroup::DATA_PROCESSING;
                        bool sf = (instr_word >> 31) & 1;
                        uint32_t opc = (instr_word >> 29) & 0x3;
                        uint32_t rd = instr_word & 0x1F;
                        uint32_t rn = (instr_word >> 5) & 0x1F;
                        // We don't fully decode the bitmask immediate here, just acknowledge the instruction
                        switch(opc) {
                            case 0: instr.id = InstructionId::AND; instr.mnemonic = "and"; break;
                            case 1: instr.id = InstructionId::ORR; instr.mnemonic = "orr"; break;
                            case 2: instr.id = InstructionId::EOR; instr.mnemonic = "eor"; break;
                            case 3: instr.id = InstructionId::ANDS; instr.mnemonic = "ands"; break;
                        }
                        instr.operands.push_back(create_reg_operand(get_reg_enum(rd, sf)));
                        instr.operands.push_back(create_reg_operand(get_reg_enum(rn, sf)));
                        // We don't fully decode the bitmask immediate here, so we add a placeholder
                        instr.operands.push_back(create_imm_operand(0));
                        // In a real scenario, we'd decode the immediate fully.
                        instr.op_str = get_reg_name(rd, sf) + ", " + get_reg_name(rn, sf) + ", #imm";
                        return;
                    }
                }

                // MOVZ
                if ((instr_word & 0x7F800000) == 0x52800000) {
                    instr.id = InstructionId::MOVZ;
                    instr.group = InstructionGroup::DATA_PROCESSING;
                    bool sf = (instr_word >> 31) & 1;
                    instr.mnemonic = "movz";
                    uint32_t rd = instr_word & 0x1F;
                    uint16_t imm16 = (instr_word >> 5) & 0xFFFF;
                    uint32_t hw = (instr_word >> 21) & 0x3;
                    uint32_t shift = hw * 16;
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rd, sf)));
                    instr.operands.push_back(create_imm_operand(imm16));
                    instr.operands.push_back(create_imm_operand(shift));
                    std::stringstream ss;
                    ss << get_reg_name(rd, sf) << ", #" << imm16;
                    if (hw > 0) {
                        ss << ", lsl #" << shift;
                    }
                    instr.op_str = ss.str();
                    return;
                }

                // MOVN
                if ((instr_word & 0x7F800000) == 0x12800000) {
                    instr.id = InstructionId::MOVN;
                    instr.group = InstructionGroup::DATA_PROCESSING;
                    bool sf = (instr_word >> 31) & 1;
                    instr.mnemonic = "movn";
                    uint32_t rd = instr_word & 0x1F;
                    uint16_t imm16 = (instr_word >> 5) & 0xFFFF;
                    uint32_t hw = (instr_word >> 21) & 0x3;
                    uint32_t shift = hw * 16;
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rd, sf)));
                    instr.operands.push_back(create_imm_operand(imm16));
                    instr.operands.push_back(create_imm_operand(shift));
                    std::stringstream ss;
                    ss << get_reg_name(rd, sf) << ", #" << imm16;
                    if (hw > 0) {
                        ss << ", lsl #" << shift;
                    }
                    instr.op_str = ss.str();
                    return;
                }

                // MOVK
                if ((instr_word & 0x7F800000) == 0x72800000) {
                    instr.id = InstructionId::MOVK;
                    instr.group = InstructionGroup::DATA_PROCESSING;
                    bool sf = (instr_word >> 31) & 1;
                    instr.mnemonic = "movk";
                    uint32_t rd = instr_word & 0x1F;
                    uint16_t imm16 = (instr_word >> 5) & 0xFFFF;
                    uint32_t hw = (instr_word >> 21) & 0x3;
                    uint32_t shift = hw * 16;
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rd, sf)));
                    instr.operands.push_back(create_imm_operand(imm16));
                    instr.operands.push_back(create_imm_operand(shift));
                    std::stringstream ss;
                    ss << get_reg_name(rd, sf) << ", #" << imm16;
                    if (hw > 0) {
                        ss << ", lsl #" << shift;
                    }
                    instr.op_str = ss.str();
                    return;
                }

                // UBFM (used for LSL, etc.)
                if ((instr_word & 0x7F800000) == 0x53000000) {
                    instr.id = InstructionId::UBFM;
                    instr.group = InstructionGroup::DATA_PROCESSING;
                    instr.mnemonic = "ubfm"; // Can be alias for lsl, etc.
                    bool sf = (instr_word >> 31) & 1;
                    uint32_t rd = instr_word & 0x1F;
                    uint32_t rn = (instr_word >> 5) & 0x1F;
                    uint32_t immr = (instr_word >> 16) & 0x3F;
                    uint32_t imms = (instr_word >> 10) & 0x3F;
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rd, sf)));
                    instr.operands.push_back(create_reg_operand(get_reg_enum(rn, sf)));
                    instr.operands.push_back(create_imm_operand(immr));
                    instr.operands.push_back(create_imm_operand(imms));
                    std::stringstream ss;
                    ss << get_reg_name(rd, sf) << ", " << get_reg_name(rn, sf) << ", #" << immr << ", #" << imms;
                    instr.op_str = ss.str();
                    return;
                }

                // Floating-point and SIMD data processing (2 source)
                if ((instr_word & 0x1E200800) == 0x1E200800) {
                    instr.group = InstructionGroup::FLOAT_SIMD;
                    uint32_t type = (instr_word >> 22) & 0x1; // 0=S, 1=D
                    uint32_t rd = instr_word & 0x1F;
                    uint32_t rn = (instr_word >> 5) & 0x1F;
                    uint32_t rm = (instr_word >> 16) & 0x1F;
                    uint32_t opcode = (instr_word >> 12) & 0xF;

                    switch (opcode) {
                        case 0b0010: instr.id = InstructionId::FADD; instr.mnemonic = "fadd"; break;
                        case 0b0011: instr.id = InstructionId::FSUB; instr.mnemonic = "fsub"; break;
                        case 0b0000: instr.id = InstructionId::FMUL; instr.mnemonic = "fmul"; break;
                        case 0b0001: instr.id = InstructionId::FDIV; instr.mnemonic = "fdiv"; break;
                        default: instr.id = InstructionId::INVALID; instr.mnemonic = "fp_op"; break;
                    }

                    if (instr.id != InstructionId::INVALID) {
                        instr.operands.push_back(create_reg_operand(get_fp_reg_enum(rd, type)));
                        instr.operands.push_back(create_reg_operand(get_fp_reg_enum(rn, type)));
                        instr.operands.push_back(create_reg_operand(get_fp_reg_enum(rm, type)));
                        std::stringstream ss;
                        ss << get_fp_reg_name(rd, type) << ", " << get_fp_reg_name(rn, type) << ", " << get_fp_reg_name(rm, type);
                        instr.op_str = ss.str();
                        return;
                    }
                }

                // Conversion between float and integer
                if ((instr_word & 0x1F000000) == 0x1E000000) {
                    uint32_t opc = (instr_word >> 16) & 0x3F;
                    // Check for SCVTF, FCVTZS
                    if (opc == 0b100010 || opc == 0b111000) {
                        instr.group = InstructionGroup::FLOAT_SIMD;
                        bool sf = (instr_word >> 31) & 1;
                        uint32_t type = (instr_word >> 22) & 0x1;
                        uint32_t rd = instr_word & 0x1F;
                        uint32_t rn = (instr_word >> 5) & 0x1F;

                        if (opc == 0b100010) { // SCVTF
                            instr.id = InstructionId::SCVTF;
                            instr.mnemonic = "scvtf";
                            instr.operands.push_back(create_reg_operand(get_fp_reg_enum(rd, type)));
                            instr.operands.push_back(create_reg_operand(get_reg_enum(rn, sf)));
                            instr.op_str = get_fp_reg_name(rd, type) + ", " + get_reg_name(rn, sf);
                        } else { // FCVTZS
                            instr.id = InstructionId::FCVTZS;
                            instr.mnemonic = "fcvtzs";
                            instr.operands.push_back(create_reg_operand(get_reg_enum(rd, sf)));
                            instr.operands.push_back(create_reg_operand(get_fp_reg_enum(rn, type)));
                            instr.op_str = get_reg_name(rd, sf) + ", " + get_fp_reg_name(rn, type);
                        }
                        return;
                    }
                }

                // FMOV (register)
                if ((instr_word & 0xFFE0FC00) == 0x1E204000) {
                    instr.id = InstructionId::FMOV;
                    instr.group = InstructionGroup::FLOAT_SIMD;
                    instr.mnemonic = "fmov";
                    uint32_t type = (instr_word >> 22) & 0x1;
                    uint32_t rd = instr_word & 0x1F;
                    uint32_t rn = (instr_word >> 5) & 0x1F;
                    instr.operands.push_back(create_reg_operand(get_fp_reg_enum(rd, type)));
                    instr.operands.push_back(create_reg_operand(get_fp_reg_enum(rn, type)));
                    instr.op_str = get_fp_reg_name(rd, type) + ", " + get_fp_reg_name(rn, type);
                    return;
                }

                // Load/Store Exclusive
                if ((instr_word & 0x3F000000) == 0x08000000) {
                    instr.group = InstructionGroup::LOAD_STORE;
                    uint32_t size = (instr_word >> 30) & 0x3;
                    bool is_load = ((instr_word >> 22) & 1);
                    uint32_t rt = instr_word & 0x1F;
                    uint32_t rn = (instr_word >> 5) & 0x1F;
                    bool is_64bit_rt = (size == 3);

                    if (is_load) {
                        instr.id = InstructionId::LDXR;
                        instr.mnemonic = "ldxr";
                        instr.operands.push_back(create_reg_operand(get_reg_enum(rt, is_64bit_rt)));
                        instr.operands.push_back(create_mem_operand(get_reg_enum(rn, true), 0));
                        instr.op_str = get_reg_name(rt, is_64bit_rt) + ", [" + get_reg_name(rn, true, true) + "]";
                    } else { // is store
                        uint32_t rs = (instr_word >> 16) & 0x1F;
                        instr.id = InstructionId::STXR;
                        instr.mnemonic = "stxr";
                        // For STXR, the status register (rs) is always 32-bit (Ws)
                        instr.operands.push_back(create_reg_operand(get_reg_enum(rs, false)));
                        instr.operands.push_back(create_reg_operand(get_reg_enum(rt, is_64bit_rt)));
                        instr.operands.push_back(create_mem_operand(get_reg_enum(rn, true), 0));
                        instr.op_str = get_reg_name(rs, false) + ", " + get_reg_name(rt, is_64bit_rt) + ", [" + get_reg_name(rn, true, true) + "]";
                    }
                    return;
                }

                instr.id = InstructionId::INVALID;
                instr.group = InstructionGroup::INVALID;
                instr.mnemonic = "unknown";
                std::stringstream ss;
                ss << "0x" << std::hex << instr_word;
                instr.op_str = ss.str();
            }
        };

        std::unique_ptr<Disassembler> CreateAArch64Disassembler() {
            return std::make_unique<AArch64Disassembler>();
        }

    } // namespace disassembler
} // namespace ur