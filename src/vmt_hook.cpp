#include <ur/vmt_hook.h>
#include <ur/memory.h>
#include <ur/thread.h>

#include <sys/mman.h>
#include <utility>

ur::VmtHook::VmtHook(void* instance) {
    vmt_address_ = *static_cast<void***>(instance);
}

std::unique_ptr<ur::VmHook> ur::VmtHook::hook_method(std::size_t index, void* hook_function) {
    thread::suspend_all_other_threads();
    void** vmt_entry_address = vmt_address_ + index;
    void* original_function = *vmt_entry_address;

    ur::memory::protect(reinterpret_cast<uintptr_t>(vmt_entry_address), sizeof(void*), PROT_READ | PROT_WRITE);
    ur::memory::write(reinterpret_cast<uintptr_t>(vmt_entry_address), &hook_function, sizeof(void*));
    ur::memory::protect(reinterpret_cast<uintptr_t>(vmt_entry_address), sizeof(void*), PROT_READ);

    thread::resume_all_other_threads();
    return std::unique_ptr<VmHook>(new VmHook(vmt_entry_address, original_function));
}

ur::VmHook::VmHook(void** vmt_entry_address, void* original_function)
    : vmt_entry_address_(vmt_entry_address), original_function_(original_function) {}

ur::VmHook::~VmHook() {
    unhook();
}

ur::VmHook::VmHook(VmHook&& other) noexcept
    : vmt_entry_address_(std::exchange(other.vmt_entry_address_, nullptr)),
      original_function_(std::exchange(other.original_function_, nullptr)) {}

ur::VmHook& ur::VmHook::operator=(VmHook&& other) noexcept {
    if (this != &other) {
        unhook();
        vmt_entry_address_ = std::exchange(other.vmt_entry_address_, nullptr);
        original_function_ = std::exchange(other.original_function_, nullptr);
    }
    return *this;
}

void ur::VmHook::unhook() {
    if (vmt_entry_address_ != nullptr) {
        thread::suspend_all_other_threads();
        ur::memory::protect(reinterpret_cast<uintptr_t>(vmt_entry_address_), sizeof(void*), PROT_READ | PROT_WRITE);
        ur::memory::write(reinterpret_cast<uintptr_t>(vmt_entry_address_), &original_function_, sizeof(void*));
        ur::memory::protect(reinterpret_cast<uintptr_t>(vmt_entry_address_), sizeof(void*), PROT_READ);
        
        vmt_entry_address_ = nullptr;
        original_function_ = nullptr;
        thread::resume_all_other_threads();
    }
}