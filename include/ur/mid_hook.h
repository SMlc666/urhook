#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include "ur/jit.h"

namespace ur::inline_hook {
class Hook;
}

namespace ur::mid_hook {

/**
 * @brief Represents the CPU context (general-purpose registers) at the time of the hook.
 * The layout is x0-x28, fp(x29), lr(x30).
 */
struct CpuContext {
    uint64_t gpr[32];
};

using Callback = void (*)(CpuContext* context);

/**
 * @brief A class for creating a hook in the middle of a function (Mid-Hook).
 *
 * This class uses an InlineHook to redirect the target function to a detour.
 * The detour saves the CPU context, calls a user-provided callback, restores
 * the context, and then executes the original instructions via the trampoline
 * provided by the InlineHook.
 */
class MidHook {
public:
    /**
     * @brief Constructs a MidHook.
     * @param target The address within a function to hook.
     * @param callback The callback function to be executed when the hook is hit.
     * @throws std::invalid_argument if target or callback are null.
     * @throws std::runtime_error if memory operations or hooking fail.
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
    void reset();
    static void dummy_callback(void*);

    // The user-provided callback.
    Callback callback_{nullptr};

    // The JIT-compiled detour function that calls the user callback.
    std::optional<ur::jit::Jit> detour_jit_;
    void* detour_{nullptr};

    // The underlying inline hook that redirects the target to our detour.
    std::unique_ptr<ur::inline_hook::Hook> inline_hook_;
};

} // namespace ur::mid_hook
