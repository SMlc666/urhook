#include "ur/mid_hook.h"
#include "ur/memory.h"
#include "ur/thread.h"
#include <stdexcept>
#include <vector>

namespace ur::mid_hook {

MidHook::MidHook(uintptr_t target, Callback callback)
    : target_address_(target), callback_(callback) {
    if (!target_address_ || !callback_) {
        throw std::invalid_argument("Target and callback must not be null.");
    }
    do_hook();
}

void MidHook::do_hook() {
    thread::suspend_all_other_threads();

    try {
        original_instructions_.resize(JUMP_INSTRUCTION_SIZE);
        if (!memory::read(target_address_, original_instructions_.data(), JUMP_INSTRUCTION_SIZE)) {
            throw std::runtime_error("Failed to read original instructions from target address.");
        }

        jit::Jit jit(target_address_);

        // Prologue
        constexpr int context_size = 256;
        jit.sub(assembler::Register::SP, assembler::Register::SP, context_size);
        for (int i = 0; i < 30; i += 2) {
            jit.stp(
                static_cast<assembler::Register>(static_cast<int>(assembler::Register::X0) + i),
                static_cast<assembler::Register>(static_cast<int>(assembler::Register::X0) + i + 1),
                assembler::Register::SP,
                i * 8
            );
        }
        jit.str(assembler::Register::LR, assembler::Register::SP, 30 * 8);

        jit.mov(assembler::Register::X0, assembler::Register::SP);
        jit.gen_abs_call(reinterpret_cast<uintptr_t>(callback_), assembler::Register::X16);

        // Epilogue
        for (int i = 0; i < 30; i += 2) {
            jit.ldp(
                static_cast<assembler::Register>(static_cast<int>(assembler::Register::X0) + i),
                static_cast<assembler::Register>(static_cast<int>(assembler::Register::X0) + i + 1),
                assembler::Register::SP,
                i * 8
            );
        }
        jit.ldr(assembler::Register::LR, assembler::Register::SP, 30 * 8);
        jit.add(assembler::Register::SP, assembler::Register::SP, context_size);
        jit.ret();

        detour_ = jit.finalize<void*>();
        if (!detour_) {
            throw std::runtime_error("Failed to allocate JIT memory for detour.");
        }

        detour_jit_.emplace(std::move(jit)); // ✅ 使用 optional

        jit::Jit branch_jit;
        branch_jit.gen_abs_jump(reinterpret_cast<uintptr_t>(detour_), assembler::Register::X16);
        const auto& branch_code_words = branch_jit.get_code();
        if (branch_code_words.empty() || branch_jit.get_code_size() != JUMP_INSTRUCTION_SIZE) {
            throw std::runtime_error("Failed to generate absolute jump instruction.");
        }
        const uint8_t* branch_code = reinterpret_cast<const uint8_t*>(branch_code_words.data());
        branch_instructions_.assign(branch_code, branch_code + JUMP_INSTRUCTION_SIZE);

        memory::protect(target_address_, JUMP_INSTRUCTION_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);

        jit::Jit loop_jit(target_address_);
        loop_jit.b(target_address_);
        memory::write(target_address_, loop_jit.get_code().data(), 4);
        memory::flush_instruction_cache(target_address_, 4);

        memory::write(target_address_ + 4, branch_code + 4, JUMP_INSTRUCTION_SIZE - 4);
        memory::flush_instruction_cache(target_address_ + 4, JUMP_INSTRUCTION_SIZE - 4);

        memory::write(target_address_, branch_code, 4);
        memory::flush_instruction_cache(target_address_, 4);

        memory::protect(target_address_, JUMP_INSTRUCTION_SIZE, PROT_READ | PROT_EXEC);

        is_enabled_ = true;
    } catch (...) {
        thread::resume_all_other_threads();
        throw;
    }

    thread::resume_all_other_threads();
}

void MidHook::do_unhook() {
    if (!is_valid()) return;

    thread::suspend_all_other_threads();

    try {
        memory::protect(target_address_, original_instructions_.size(), PROT_READ | PROT_WRITE | PROT_EXEC);

        jit::Jit loop_jit(target_address_);
        loop_jit.b(target_address_);
        memory::write(target_address_, loop_jit.get_code().data(), 4);
        memory::flush_instruction_cache(target_address_, 4);

        if (original_instructions_.size() > 4) {
            memory::write(target_address_ + 4, original_instructions_.data() + 4, original_instructions_.size() - 4);
            memory::flush_instruction_cache(target_address_ + 4, original_instructions_.size() - 4);
        }

        memory::write(target_address_, original_instructions_.data(), 4);
        memory::flush_instruction_cache(target_address_, 4);

        memory::protect(target_address_, original_instructions_.size(), PROT_READ | PROT_EXEC);
    } catch (...) {
        thread::resume_all_other_threads();
        throw;
    }

    thread::resume_all_other_threads();
}

void MidHook::unhook() {
    if (is_enabled_) {
        do_unhook();
        is_enabled_ = false;
    }
}

bool MidHook::enable() {
    if (!is_valid() || is_enabled_) {
        return false;
    }

    thread::suspend_all_other_threads();

    memory::protect(target_address_, JUMP_INSTRUCTION_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);
    memory::write(target_address_, branch_instructions_.data(), JUMP_INSTRUCTION_SIZE);
    memory::protect(target_address_, JUMP_INSTRUCTION_SIZE, PROT_READ | PROT_EXEC);
    memory::flush_instruction_cache(target_address_, JUMP_INSTRUCTION_SIZE);

    is_enabled_ = true;

    thread::resume_all_other_threads();
    return true;
}

void MidHook::reset() {
    target_address_ = 0;
    callback_ = nullptr;
    original_instructions_.clear();
    branch_instructions_.clear();
    detour_ = nullptr;
    is_enabled_ = false;
    detour_jit_.reset(); // ✅ 释放 optional<Jit> 中的资源
}
bool MidHook::disable() {
    if (!is_valid() || !is_enabled_) {
        return false;
    }
    do_unhook();
    is_enabled_ = false;
    return true;
}
MidHook::~MidHook() {
    unhook();
    reset();
}

MidHook::MidHook(MidHook&& other) noexcept
    : target_address_(other.target_address_),
      callback_(other.callback_),
      original_instructions_(std::move(other.original_instructions_)),
      detour_jit_(std::move(other.detour_jit_)),
      detour_(other.detour_),
      is_enabled_(other.is_enabled_) {
    other.reset();
}

MidHook& MidHook::operator=(MidHook&& other) noexcept {
    if (this != &other) {
        unhook();
        target_address_ = other.target_address_;
        callback_ = other.callback_;
        original_instructions_ = std::move(other.original_instructions_);
        detour_jit_ = std::move(other.detour_jit_);
        detour_ = other.detour_;
        is_enabled_ = other.is_enabled_;
        other.reset();
    }
    return *this;
}

bool MidHook::is_valid() const {
    return target_address_ != 0 && detour_ != nullptr;
}

} // namespace ur::mid_hook