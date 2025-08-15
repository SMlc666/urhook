#include "ur/mid_hook.h"
#include "ur/inline_hook.h"
#include <stdexcept>
#include <utility>

namespace ur::mid_hook {

// Constructor implementation
MidHook::MidHook(uintptr_t target, Callback callback)
    : callback_(callback) {
    if (target == 0 || callback == nullptr) {
        throw std::invalid_argument("Target and callback must not be null.");
    }

    // 1. Create the underlying inline hook in a disabled state to get the trampoline.
    inline_hook_ = std::make_unique<ur::inline_hook::Hook>(target, (ur::inline_hook::Hook::Callback)dummy_callback, false);
    if (!inline_hook_->is_valid()) {
        throw std::runtime_error("Failed to initialize the underlying inline hook.");
    }
    uintptr_t trampoline = inline_hook_->get_trampoline();
    if (trampoline == 0) {
        throw std::runtime_error("Failed to get trampoline from inline hook.");
    }

    // 2. JIT-compile the detour function.
    auto jit = std::make_optional<ur::jit::Jit>();

    // Prologue: Save context
    constexpr int context_size = sizeof(CpuContext);
    jit->sub(assembler::Register::SP, assembler::Register::SP, context_size);
    for (int i = 0; i < 30; i += 2) {
        jit->stp(
            static_cast<assembler::Register>(static_cast<int>(assembler::Register::X0) + i),
            static_cast<assembler::Register>(static_cast<int>(assembler::Register::X0) + i + 1),
            assembler::Register::SP,
            i * 8
        );
    }
    jit->str(assembler::Register::LR, assembler::Register::SP, 30 * 8);

    // Call the user-provided callback
    jit->mov(assembler::Register::X0, assembler::Register::SP); // Pass context pointer
    jit->gen_abs_call(reinterpret_cast<uintptr_t>(callback_), assembler::Register::X16);

    // Epilogue: Restore context
    for (int i = 0; i < 30; i += 2) {
        jit->ldp(
            static_cast<assembler::Register>(static_cast<int>(assembler::Register::X0) + i),
            static_cast<assembler::Register>(static_cast<int>(assembler::Register::X0) + i + 1),
            assembler::Register::SP,
            i * 8
        );
    }
    jit->ldr(assembler::Register::LR, assembler::Register::SP, 30 * 8);
    jit->add(assembler::Register::SP, assembler::Register::SP, context_size);

    // Jump to the original instructions (trampoline)
    jit->gen_abs_jump(trampoline, assembler::Register::X16);

    detour_ = jit->finalize<void*>();
    if (!detour_) {
        throw std::runtime_error("Failed to allocate JIT memory for detour.");
    }
    detour_jit_ = std::move(jit);

    // 3. Set the real detour and enable the hook.
    inline_hook_->set_detour((ur::inline_hook::Hook::Callback)detour_);
    inline_hook_->enable();
}

// Destructor: Let the member destructors handle cleanup (RAII).
// The destruction order (reverse of declaration) is correct:
// 1. inline_hook_ is destructed -> unhooks the target.
// 2. detour_jit_ is destructed -> frees the JIT memory.
MidHook::~MidHook() = default;

void MidHook::unhook() {
    if (inline_hook_) {
        // The unique_ptr's destructor will call the Hook's destructor,
        // which in turn calls unhook. So we just need to reset it.
        inline_hook_.reset();
    }
    // After unhooking, we can safely release the resources.
    detour_jit_.reset();
    detour_ = nullptr;
    callback_ = nullptr;
}

bool MidHook::enable() {
    return inline_hook_ ? inline_hook_->enable() : false;
}

bool MidHook::disable() {
    return inline_hook_ ? inline_hook_->disable() : false;
}

bool MidHook::is_valid() const {
    return inline_hook_ && inline_hook_->is_valid();
}

MidHook::MidHook(MidHook&& other) noexcept
    : callback_(other.callback_),
      detour_jit_(std::move(other.detour_jit_)),
      detour_(other.detour_),
      inline_hook_(std::move(other.inline_hook_)) {
    // Invalidate the moved-from object so its destructor does nothing.
    other.callback_ = nullptr;
    other.detour_ = nullptr;
}

MidHook& MidHook::operator=(MidHook&& other) noexcept {
    if (this != &other) {
        // By assigning new members, the old members are destructed in the correct order.
        // 1. Old inline_hook_ is destructed (unhooks).
        // 2. Old detour_jit_ is destructed (frees JIT memory).
        inline_hook_ = std::move(other.inline_hook_);
        detour_jit_ = std::move(other.detour_jit_);
        detour_ = other.detour_;
        callback_ = other.callback_;

        // Invalidate the moved-from object.
        other.callback_ = nullptr;
        other.detour_ = nullptr;
    }
    return *this;
}

// A dummy callback to satisfy the initial hook creation.
void MidHook::dummy_callback(void*) {}

} // namespace ur::mid_hook
