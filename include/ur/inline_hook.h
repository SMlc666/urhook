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

    Hook(uintptr_t target, Callback callback);
    ~Hook();

    Hook(const Hook&) = delete;
    Hook& operator=(const Hook&) = delete;

    Hook(Hook&& other) noexcept;
    Hook& operator=(Hook&& other) noexcept;

    bool is_valid() const;

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
    void unhook();
    void reset();

    uintptr_t target_address_{0};
    Callback callback_{nullptr};
    void* original_func_{nullptr}; // Points to the trampoline
    void* dispatcher_{nullptr};    // Points to the generated dispatcher code for this hook
};

} // namespace ur::inline_hook
