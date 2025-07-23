#include "ur/jit.h"
#include <utility>
#include <stdexcept>

namespace ur::jit {

    uint32_t Label::next_id_ = 0;

    Jit::Jit(uintptr_t address) : Assembler(address) {}

    Jit::~Jit() {
        if (mem_ != nullptr) {
            munmap(mem_, size_);
        }
    }

    Jit::Jit(Jit&& other) noexcept : Assembler(std::move(other)) {
        mem_ = other.mem_;
        size_ = other.size_;
        label_patches_ = std::move(other.label_patches_);
        other.mem_ = nullptr;
        other.size_ = 0;
    }

    Jit& Jit::operator=(Jit&& other) noexcept {
        if (this != &other) {
            Assembler::operator=(std::move(other));
            if (mem_ != nullptr) {
                munmap(mem_, size_);
            }
            mem_ = other.mem_;
            size_ = other.size_;
            label_patches_ = std::move(other.label_patches_);
            other.mem_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    void* Jit::release() {
        void* released_mem = mem_;
        mem_ = nullptr;
        size_ = 0;
        return released_mem;
    }

    Label Jit::new_label() {
        return Label();
    }

    void Jit::bind(Label& label) {
        if (label.is_bound()) {
            throw std::runtime_error("Label is already bound");
        }

        auto& code = get_code_mut();
        uint32_t current_offset = code.size() * 4;
        label.offset_ = current_offset;
        label.bound_ = true;

        // Patch all previous references to this label
        for (auto const& patch_site_offset : label.references_) {
            uint32_t instruction = code[patch_site_offset / 4];
            int32_t relative_offset = current_offset - patch_site_offset;

            // This logic assumes branch instructions (B and B.cond)
            // The offset is encoded in the lower 26 bits for B, and 19 bits for B.cond
            if ((instruction & 0xFC000000) == 0x14000000) { // Unconditional Branch (B)
                uint32_t imm26 = (relative_offset >> 2) & 0x3FFFFFF;
                code[patch_site_offset / 4] |= imm26;
            } else if ((instruction & 0xFF000010) == 0x54000000) { // Conditional Branch (B.cond)
                uint32_t imm19 = (relative_offset >> 2) & 0x7FFFF;
                code[patch_site_offset / 4] |= (imm19 << 5);
            }
        }
        label.references_.clear(); // All patched
    }

    void Jit::b(Label& label) {
        if (label.is_bound()) {
            // If the label is already bound, we know the target address
            Assembler::b(get_current_address() + (label.offset() - get_code_size()));
        } else {
            // Label is not bound yet, emit a placeholder and record for patching
            uint32_t current_offset = get_code_size();
            label.references_.push_back(current_offset);
            Assembler::b(get_current_address()); // Emit B instruction pointing to itself
        }
    }

    void Jit::b(assembler::Condition cond, Label& label) {
        if (label.is_bound()) {
            Assembler::b(cond, get_current_address() + (label.offset() - get_code_size()));
        } else {
            uint32_t current_offset = get_code_size();
            label.references_.push_back(current_offset);
            Assembler::b(cond, get_current_address()); // Emit B.cond pointing to itself
        }
    }

}