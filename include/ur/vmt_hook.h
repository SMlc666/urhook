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

    private:
        friend class VmtHook;
        VmHook(void** vmt_entry_address, void* original_function);

        void unhook();

        void** vmt_entry_address_{nullptr};
        void* original_function_{nullptr};
    };
}
