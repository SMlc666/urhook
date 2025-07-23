#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include "ur/jit.h"

namespace ur::mid_hook {

/**
 * @brief Represents the CPU context (general-purpose registers) at the time of the hook.
 * The layout is x0-x28, fp(x29), lr(x30).
 */
struct CpuContext {
    uint64_t gpr[31];
};

using Callback = void (*)(CpuContext* context);

/**
 * @brief A class for creating a hook in the middle of a function (Mid-Hook).
 *
 * This class overwrites instructions at the target address with a jump
 * to a dynamically generated detour. The detour saves the CPU context, calls a
 * user-provided callback, restores the context, executes the original instructions,
 * and finally jumps back. It uses an absolute jump to support long-distance detours.
 */
class MidHook {
public:
    /**
     * @brief Constructs a MidHook.
     * @param target The address within a function to hook.
     * @param callback The callback function to be executed when the hook is hit.
     * @throws std::invalid_argument if target or callback are null.
     * @throws std::runtime_error if memory operations fail.
     */
    MidHook(uintptr_t target, Callback callback);

    /**
     * @brief Destructor that automatically unhooks.
     */
    ~MidHook();

    // Non-copyable
    MidHook(const MidHook&) = delete;
    MidHook& operator=(const MidHook&) = delete;

    // Movable
    MidHook(MidHook&& other) noexcept;
    MidHook& operator=(MidHook&& other) noexcept;

    /**
     * @brief Checks if the hook is currently active and valid.
     * @return True if the hook is valid, false otherwise.
     */
    bool is_valid() const;

    /**
     * @brief Manually unhooks the function, restoring the original code.
     */
    void unhook();

    /**
     * @brief Enables the hook if it was previously disabled.
     * @return True on success, false otherwise.
     */
    bool enable();

    /**
     * @brief Disables the hook without removing it, allowing it to be re-enabled later.
     * @return True on success, false otherwise.
     */
    bool disable();

private:
    void do_hook();
    void do_unhook();
    void reset();

    static constexpr size_t JUMP_INSTRUCTION_SIZE = assembler::Assembler::ABS_JUMP_SIZE;

    uintptr_t target_address_{0};
    Callback callback_{nullptr};
    std::vector<uint8_t> original_instructions_{};
    std::vector<uint8_t> branch_instructions_{};
    std::optional<ur::jit::Jit> detour_jit_;
    void* detour_{nullptr};
    bool is_enabled_{false};
};

} // namespace ur::mid_hook
