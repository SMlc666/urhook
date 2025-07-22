#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>

namespace ur {
    namespace disassembler {

        enum class InstructionId {
            INVALID = 0,
            // Data Processing
            ADD,
            SUB,
            SUBS,
            ADDS,
            AND,
            ORR,
            EOR,
            ANDS,
            MOV,
            MOVZ,
            MOVK,
            ADR,
            ADRP,
            // Branch
            B,
            BL,
            BR,
            BLR,
            B_COND,
            CBZ,
            CBNZ,
            RET,
            // Load/Store
            LDR,
            STR,
            LDP,
            STP,
            LDR_LIT,
            // System
            NOP,
        };

        enum class InstructionGroup {
            INVALID = 0,
            JUMP,
            DATA_PROCESSING,
            LOAD_STORE,
            SYSTEM,
        };

        // Represents a single disassembled instruction.
        struct Instruction {
            uint64_t address;
            uint32_t size;
            std::vector<uint8_t> bytes;
            InstructionId id = InstructionId::INVALID;
            InstructionGroup group = InstructionGroup::INVALID;
            std::string mnemonic; // e.g., "mov", "add"
            std::string op_str;   // e.g., "x0, x1"
            bool is_pc_relative = false;
        };

        // Abstract base class for a disassembler.
        class Disassembler {
        public:
            virtual ~Disassembler() = default;

            // Disassembles instructions from a given memory address.
            //
            // @param address The memory address to start disassembling from.
            // @param code A pointer to the code bytes.
            // @param code_size The size of the code bytes.
            // @param count The maximum number of instructions to disassemble.
            // @return A vector of disassembled instructions.
            virtual std::vector<Instruction> Disassemble(
                uint64_t address,
                const uint8_t* code,
                size_t code_size,
                size_t count
            ) = 0;
        };

        // Factory function to create a concrete disassembler instance.
        std::unique_ptr<Disassembler> CreateAArch64Disassembler();

    } // namespace disassembler
} // namespace ur