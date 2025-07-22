#include <ur/vmt_hook.h>
#include <ur/memory.h>
#include <ur/thread.h>

#include <sys/mman.h>
#include <utility>

ur::VmtHook::VmtHook(void* instance) {
    vmt_address_ = *static_cast<void***>(instance);
}

std::unique_ptr<ur::VmHook> ur::VmtHook::hook_method(std::size_t index, void* hook_function) {
    void** vmt_entry_address = vmt_address_ + index;
    void* original_function = *vmt_entry_address;

    auto hook = std::unique_ptr<VmHook>(new VmHook(vmt_entry_address, hook_function, original_function));
    hook->enable();
    return hook;
}

ur::VmHook::VmHook(void** vmt_entry_address, void* hook_function, void* original_function)
    : vmt_entry_address_(vmt_entry_address),
      hook_function_(hook_function),
      original_function_(original_function),
      is_enabled_(false) {}

ur::VmHook::~VmHook() {
    unhook();
}

ur::VmHook::VmHook(VmHook&& other) noexcept
    : vmt_entry_address_(std::exchange(other.vmt_entry_address_, nullptr)),
      hook_function_(std::exchange(other.hook_function_, nullptr)),
      original_function_(std::exchange(other.original_function_, nullptr)),
      is_enabled_(std::exchange(other.is_enabled_, false)) {}

ur::VmHook& ur::VmHook::operator=(VmHook&& other) noexcept {
    if (this != &other) {
        unhook();
        vmt_entry_address_ = std::exchange(other.vmt_entry_address_, nullptr);
        hook_function_ = std::exchange(other.hook_function_, nullptr);
        original_function_ = std::exchange(other.original_function_, nullptr);
        is_enabled_ = std::exchange(other.is_enabled_, false);
    }
    return *this;
}

void ur::VmHook::unhook() {
    if (is_enabled_) {
        disable();
    }
    vmt_entry_address_ = nullptr;
    hook_function_ = nullptr;
    original_function_ = nullptr;
}

bool ur::VmHook::enable() {
    if (vmt_entry_address_ == nullptr || is_enabled_) {
        return false;
    }
    thread::suspend_all_other_threads();
    ur::memory::protect(reinterpret_cast<uintptr_t>(vmt_entry_address_), sizeof(void*), PROT_READ | PROT_WRITE);
    ur::memory::write(reinterpret_cast<uintptr_t>(vmt_entry_address_), &hook_function_, sizeof(void*));
    ur::memory::protect(reinterpret_cast<uintptr_t>(vmt_entry_address_), sizeof(void*), PROT_READ);
    is_enabled_ = true;
    thread::resume_all_other_threads();
    return true;
}

bool ur::VmHook::disable() {
    if (vmt_entry_address_ == nullptr || !is_enabled_) {
        return false;
    }
    thread::suspend_all_other_threads();
    ur::memory::protect(reinterpret_cast<uintptr_t>(vmt_entry_address_), sizeof(void*), PROT_READ | PROT_WRITE);
    ur::memory::write(reinterpret_cast<uintptr_t>(vmt_entry_address_), &original_function_, sizeof(void*));
    ur::memory::protect(reinterpret_cast<uintptr_t>(vmt_entry_address_), sizeof(void*), PROT_READ);
    is_enabled_ = false;
    thread::resume_all_other_threads();
    return true;
}
