#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <memory>

namespace ur::inline_hook {

// Forward declaration
class Hook;

/**
 * @brief Calls the original function (or the next hook in the chain).
 *
 * This function should be called from within your hook callback. It looks up
 * the correct function to call for the current thread and forwards the arguments.
 *
 * @tparam Ret The return type of the original function.
 * @tparam Args The argument types of the original function.
 * @param args The arguments to pass to the original function.
 * @return The return value from the original function.
 */


class Hook {
public:
    using Callback = void*;

    Hook(uintptr_t target, Callback callback, bool enable_now = true);
    ~Hook();

    Hook(const Hook&) = delete;
    Hook& operator=(const Hook&) = delete;

    Hook(Hook&& other) noexcept;
    Hook& operator=(Hook&& other) noexcept;

    bool is_valid() const;

    uintptr_t get_trampoline() const;

    void set_detour(Callback callback);

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

    // This is for calling the original function from OUTSIDE the hook chain.
    template<typename Ret, typename... Args>
    Ret call_original(Args... args) const {
        if (!is_valid()) {
            throw std::runtime_error("Hook is not valid or has been moved.");
        }
        using OriginalFuncType = Ret (*)(Args...);
        auto func = reinterpret_cast<OriginalFuncType>(original_func_);
        return func(args...);
    }

private:
    void do_unhook();
    void reset();

    uintptr_t target_address_{0};
    Callback callback_{nullptr};
    void* original_func_{nullptr}; // Points to the trampoline
    bool is_enabled_{false};
};

} // namespace ur::inline_hook
