#pragma once

#include <memory>
#include <functional>

namespace ur {
    class VmHook;

    class VmtHook {
    public:
        explicit VmtHook(void* instance);
        ~VmtHook() = default;

        VmtHook(const VmtHook&) = delete;
        VmtHook& operator=(const VmtHook&) = delete;
        VmtHook(VmtHook&&) = delete;
        VmtHook& operator=(VmtHook&&) = delete;

        [[nodiscard]] std::unique_ptr<VmHook> hook_method(std::size_t index, void* hook_function);

    private:
        void** vmt_address_;
    };

    class VmHook {
    public:
        ~VmHook();

        VmHook(const VmHook&) = delete;
        VmHook& operator=(const VmHook&) = delete;
        VmHook(VmHook&& other) noexcept;
        VmHook& operator=(VmHook&& other) noexcept;

        template <typename T>
        T get_original() const {
            return reinterpret_cast<T>(original_function_);
        }

        /**
         * @brief Manually unhooks the method, restoring the original function pointer.
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
        friend class VmtHook;
        VmHook(void** vmt_entry_address, void* hook_function, void* original_function);

        void** vmt_entry_address_{nullptr};
        void* hook_function_{nullptr};
        void* original_function_{nullptr};
        bool is_enabled_{false};
    };
}
