#include "ur/inline_hook.h"
#include "ur/memory.h"
#include "ur/assembler.h"
#include "ur/disassembler.h"

#include <map>
#include <mutex>
#include <list>
#include <stdexcept>
#include <algorithm>
#include <sys/mman.h>
#include <unistd.h>

namespace ur::inline_hook {

// --- Helper Functions & Data Structures ---

namespace {

// Represents a single link in the hook chain.
struct HookEntry {
    Hook* owner = nullptr; // Back-pointer to the Hook object
    Hook::Callback callback = nullptr;
    void* call_next = nullptr; // Address to call for the "original" function
    bool is_enabled = true;
};

// Holds all information about a hooked target address.
struct HookInfo {
    uintptr_t target_address = 0;
    std::list<HookEntry> entries; // A list to represent the call chain
    std::vector<uint8_t> original_code;
    void* trampoline = nullptr; // Jumps to the original function's remainder
    size_t backup_size = 0;
    std::mutex info_mutex;
};

// Global map to manage all active hooks.
static std::map<uintptr_t, std::unique_ptr<HookInfo>> g_hooks;
static std::mutex g_hooks_mutex;

void* allocate_executable_memory(size_t size) {
    long page_size = sysconf(_SC_PAGESIZE);
    size_t aligned_size = (size + page_size - 1) & -page_size;
    void* mem = mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    return mem == MAP_FAILED ? nullptr : mem;
}

void free_executable_memory(void* mem, size_t size) {
    if (mem) {
        long page_size = sysconf(_SC_PAGESIZE);
        size_t aligned_size = (size + page_size - 1) & -page_size;
        munmap(mem, aligned_size);
    }
}

bool patch_target(uintptr_t target, uintptr_t destination) {
    assembler::Assembler assembler(target);
    assembler.gen_abs_jump(destination, assembler::Register::X16);
    const auto& patch_code_vec = assembler.get_code();
    const auto* patch_code = reinterpret_cast<const uint8_t*>(patch_code_vec.data());
    size_t patch_size = patch_code_vec.size() * sizeof(uint32_t);

    return memory::atomic_patch(target, patch_code, patch_size);
}

bool restore_target(HookInfo& info) {
    return memory::atomic_patch(info.target_address, info.original_code.data(), info.backup_size);
}

// --- Trampoline Relocation Logic ---

// Relocates instructions from the target function to a trampoline.
// Returns the relocated machine code.
std::vector<uint32_t> relocate_trampoline(uintptr_t target, uintptr_t trampoline_addr, size_t& backup_size) {
    auto disassembler = disassembler::CreateAArch64Disassembler();
    const uint8_t* code = reinterpret_cast<const uint8_t*>(target);
    const size_t required_size = assembler::Assembler::ABS_JUMP_SIZE;
    
    auto instructions = disassembler->Disassemble(target, code, 100, 20); // Disassemble up to 20 instructions
    if (instructions.empty()) {
        throw std::runtime_error("Failed to disassemble target function");
    }

    assembler::Assembler tramp_asm(trampoline_addr);
    backup_size = 0;

    for (size_t i = 0; i < instructions.size(); ++i) {
        auto& current_insn = instructions[i];

        if (current_insn.is_pc_relative) {
            // Handle ADRP instruction
            if (current_insn.id == disassembler::InstructionId::ADRP) {
                bool pair_relocated = false;
                uintptr_t page_addr = std::get<int64_t>(current_insn.operands[1].value);

                if (i + 1 < instructions.size()) {
                    auto& next_insn = instructions[i + 1];
                    auto adrp_dest_reg = std::get<assembler::Register>(current_insn.operands[0].value);
                    uintptr_t final_addr;

                    // Case 1: ADRP + ADD
                    if (next_insn.id == disassembler::InstructionId::ADD && next_insn.operands.size() > 2 &&
                        next_insn.operands[1].type == disassembler::OperandType::REGISTER && std::get<assembler::Register>(next_insn.operands[1].value) == adrp_dest_reg &&
                        next_insn.operands[2].type == disassembler::OperandType::IMMEDIATE) {

                        final_addr = page_addr + std::get<int64_t>(next_insn.operands[2].value);
                        auto dest_reg = std::get<assembler::Register>(next_insn.operands[0].value);
                        tramp_asm.gen_load_address(dest_reg, final_addr);
                        pair_relocated = true;
                    }
                    // Case 2: ADRP + LDR/STR (memory access)
                    else if ((next_insn.id == disassembler::InstructionId::LDR || next_insn.id == disassembler::InstructionId::STR) && next_insn.operands.size() > 1 &&
                             next_insn.operands[1].type == disassembler::OperandType::MEMORY) {
                        auto mem_op = std::get<disassembler::MemOperand>(next_insn.operands[1].value);
                        if (mem_op.base == adrp_dest_reg) {
                            final_addr = page_addr + mem_op.displacement;
                            auto data_reg = std::get<assembler::Register>(next_insn.operands[0].value);

                            tramp_asm.gen_load_address(assembler::Register::X16, final_addr);
                            if (next_insn.id == disassembler::InstructionId::LDR) {
                                tramp_asm.ldr(data_reg, assembler::Register::X16, 0);
                            } else { // STR
                                tramp_asm.str(data_reg, assembler::Register::X16, 0);
                            }
                            pair_relocated = true;
                        }
                    }
                }

                if (pair_relocated) {
                    backup_size += current_insn.size + instructions[i+1].size;
                    i++; // Consumed two instructions
                } else {
                    // If not a recognized pair or last instruction, just relocate the ADRP itself.
                    auto dest_reg = std::get<assembler::Register>(current_insn.operands[0].value);
                    tramp_asm.gen_load_address(dest_reg, page_addr);
                    backup_size += current_insn.size;
                }
            }
            // Handle LDR (literal)
            else if (current_insn.id == disassembler::InstructionId::LDR_LIT) {
                 uintptr_t target_addr = std::get<int64_t>(current_insn.operands[1].value);
                 auto dest_reg = std::get<assembler::Register>(current_insn.operands[0].value);
                 tramp_asm.gen_load_address(assembler::Register::X16, target_addr);
                 tramp_asm.ldr(dest_reg, assembler::Register::X16, 0);
                 backup_size += current_insn.size;
            }
            // Handle branches
            else if (current_insn.group == disassembler::InstructionGroup::JUMP) {
                uintptr_t target_addr = std::get<int64_t>(current_insn.operands[0].value);

                if (current_insn.id == disassembler::InstructionId::B_COND) {
                    tramp_asm.b(current_insn.cond, target_addr);
                } else {
                    tramp_asm.gen_load_address(assembler::Register::X16, target_addr);
                    if (current_insn.id == disassembler::InstructionId::BL) {
                        tramp_asm.blr(assembler::Register::X16);
                    } else { // B, CBZ, CBNZ, TBZ, TBNZ etc.
                        tramp_asm.br(assembler::Register::X16);
                    }
                }
                backup_size += current_insn.size;
            } else {
                 // Other PC-relative, copy it for now. This might be an issue.
                uint32_t instruction_word = *reinterpret_cast<const uint32_t*>(current_insn.bytes.data());
                tramp_asm.get_code_mut().push_back(instruction_word);
                backup_size += current_insn.size;
            }
        } else {
            // Not a PC-relative instruction, just copy it
            uint32_t instruction_word = *reinterpret_cast<const uint32_t*>(current_insn.bytes.data());
            tramp_asm.get_code_mut().push_back(instruction_word);
            backup_size += current_insn.size;
        }

        if (backup_size >= required_size) break;
    }

    return tramp_asm.get_code();
}


} // namespace

// --- Hook Class Implementation ---

Hook::Hook(uintptr_t target, Callback callback, bool enable_now) {
    if (target == 0) {
        throw std::invalid_argument("Target must not be null");
    }
     if (callback == nullptr && enable_now) {
        throw std::invalid_argument("Callback must not be null if hook is enabled immediately");
    }

    try {
        std::lock_guard<std::mutex> lock(g_hooks_mutex);
        if (g_hooks.find(target) == g_hooks.end()) {
            g_hooks[target] = std::make_unique<HookInfo>();
        }
        
        auto& info = *g_hooks[target];
        std::lock_guard<std::mutex> info_lock(info.info_mutex);

        target_address_ = target;
        callback_ = callback;
        info.target_address = target;

        if (info.trampoline == nullptr) {
            auto relocated_code = relocate_trampoline(target, 0, info.backup_size);
            size_t relocated_size = relocated_code.size() * sizeof(uint32_t);

            size_t trampoline_size = relocated_size + assembler::Assembler::ABS_JUMP_SIZE;
            info.trampoline = allocate_executable_memory(trampoline_size);
            if (!info.trampoline) throw std::runtime_error("Failed to allocate trampoline memory");

            // Copy the relocated code into the trampoline
            memcpy(info.trampoline, relocated_code.data(), relocated_size);
            
            // Add the jump back to the original function
            assembler::Assembler tramp_asm(reinterpret_cast<uintptr_t>(info.trampoline) + relocated_size);
            tramp_asm.gen_abs_jump(target + info.backup_size, assembler::Register::X16);
            
            // Copy the generated jump code to the trampoline
            const auto& tramp_jump_code = tramp_asm.get_code();
            memcpy(reinterpret_cast<char*>(info.trampoline) + relocated_size, tramp_jump_code.data(), tramp_jump_code.size() * sizeof(uint32_t));

            // Save original code
            info.original_code.assign(reinterpret_cast<uint8_t*>(target), reinterpret_cast<uint8_t*>(target) + info.backup_size);
            
            __builtin___clear_cache(reinterpret_cast<char*>(info.trampoline), reinterpret_cast<char*>(info.trampoline) + trampoline_size);
        }

        void* next_func_to_call = info.entries.empty() ? info.trampoline : info.entries.front().callback;
        original_func_ = next_func_to_call;

        info.entries.push_front({this, callback, next_func_to_call, enable_now});

        if (enable_now) {
            patch_target(target, reinterpret_cast<uintptr_t>(callback));
        }
        is_enabled_ = enable_now;
    } catch (...) {
        throw;
    }
}

Hook::~Hook() {
    unhook();
}

void Hook::set_detour(Callback callback) {
    if (!is_valid()) return;
    
    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    auto it = g_hooks.find(target_address_);
    if (it == g_hooks.end()) return;

    auto& info = *it->second;
    std::lock_guard<std::mutex> info_lock(info.info_mutex);

    auto entry_it = std::find_if(info.entries.begin(), info.entries.end(), 
        [this](const HookEntry& entry) { return entry.owner == this; });

    if (entry_it != info.entries.end()) {
        entry_it->callback = callback;
        this->callback_ = callback;
        
        // If this is the currently active hook, re-patch the target
        if (is_enabled_ && info.entries.front().owner == this) {
            patch_target(target_address_, reinterpret_cast<uintptr_t>(callback));
        }
    }
}

void Hook::do_unhook() {
    if (!is_valid()) return;

    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    auto it = g_hooks.find(target_address_);
    if (it == g_hooks.end()) {
        reset();
        return;
    }

    auto& info = *it->second;
    std::lock_guard<std::mutex> info_lock(info.info_mutex);

    auto entry_it = std::find_if(info.entries.begin(), info.entries.end(), 
        [this](const HookEntry& entry) { return entry.owner == this; });

    if (entry_it != info.entries.end()) {
        auto next_it = std::next(entry_it);
        if (entry_it != info.entries.begin()) {
            auto prev_it = std::prev(entry_it);
            if (next_it != info.entries.end()) {
                prev_it->call_next = next_it->callback;
            } else {
                prev_it->call_next = info.trampoline;
            }
        }
        info.entries.erase(entry_it);
    }

    if (info.entries.empty()) {
        restore_target(info);
        free_executable_memory(info.trampoline, info.backup_size + assembler::Assembler::ABS_JUMP_SIZE);
        g_hooks.erase(it);
    } else {
        patch_target(target_address_, reinterpret_cast<uintptr_t>(info.entries.front().callback));
    }

    reset();
}

void Hook::unhook() {
    // No need to check is_enabled_ here, unhook should work even if disabled.
    do_unhook();
}

bool Hook::enable() {
    if (!is_valid() || is_enabled_) {
        return false;
    }
     if (callback_ == nullptr) { // Cannot enable a hook without a detour
        return false;
    }

    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    auto it = g_hooks.find(target_address_);
    if (it == g_hooks.end()) {
        return false;
    }

    auto& info = *it->second;
    std::lock_guard<std::mutex> info_lock(info.info_mutex);

    auto entry_it = std::find_if(info.entries.begin(), info.entries.end(),
        [this](const HookEntry& entry) { return entry.owner == this; });

    if (entry_it != info.entries.end()) {
        entry_it->is_enabled = true;
    }
    is_enabled_ = true; // Update flag before patching

    // Re-patch to the first enabled hook
    auto first_enabled = std::find_if(info.entries.begin(), info.entries.end(),
        [](const HookEntry& entry) { return entry.is_enabled; });

    if (first_enabled != info.entries.end()) {
        patch_target(target_address_, reinterpret_cast<uintptr_t>(first_enabled->callback));
    } else {
        restore_target(info);
    }

    return true;
}

bool Hook::disable() {
    if (!is_valid() || !is_enabled_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    auto it = g_hooks.find(target_address_);
    if (it == g_hooks.end()) {
        return false;
    }

    auto& info = *it->second;
    std::lock_guard<std::mutex> info_lock(info.info_mutex);

    auto entry_it = std::find_if(info.entries.begin(), info.entries.end(),
        [this](const HookEntry& entry) { return entry.owner == this; });

    if (entry_it != info.entries.end()) {
        entry_it->is_enabled = false;
    }
    is_enabled_ = false; // Update flag before patching

    // Re-patch to the first enabled hook
    auto first_enabled = std::find_if(info.entries.begin(), info.entries.end(),
        [](const HookEntry& entry) { return entry.is_enabled; });

    if (first_enabled != info.entries.end()) {
        patch_target(target_address_, reinterpret_cast<uintptr_t>(first_enabled->callback));
    } else {
        restore_target(info);
    }

    return true;
}

Hook::Hook(Hook&& other) noexcept
    : target_address_(other.target_address_),
      callback_(other.callback_),
      original_func_(other.original_func_),
      is_enabled_(other.is_enabled_) {
    if (!is_valid()) return;
    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    auto it = g_hooks.find(target_address_);
    if (it != g_hooks.end()) {
        auto& info = *it->second;
        std::lock_guard<std::mutex> info_lock(info.info_mutex);
        auto entry_it = std::find_if(info.entries.begin(), info.entries.end(), 
            [&other](const HookEntry& entry) { return entry.owner == &other; });
        if (entry_it != info.entries.end()) {
            entry_it->owner = this;
        }
    }
    other.reset();
}

Hook& Hook::operator=(Hook&& other) noexcept {
    if (this != &other) {
        unhook();
        target_address_ = other.target_address_;
        callback_ = other.callback_;
        original_func_ = other.original_func_;
        is_enabled_ = other.is_enabled_;
        
        if (is_valid()) {
            std::lock_guard<std::mutex> lock(g_hooks_mutex);
            auto it = g_hooks.find(target_address_);
            if (it != g_hooks.end()) {
                auto& info = *it->second;
                std::lock_guard<std::mutex> info_lock(info.info_mutex);
                auto entry_it = std::find_if(info.entries.begin(), info.entries.end(), 
                    [&other](const HookEntry& entry) { return entry.owner == &other; });
                if (entry_it != info.entries.end()) {
                    entry_it->owner = this;
                }
            }
        }
        other.reset();
    }
    return *this;
}

bool Hook::is_valid() const {
    return target_address_ != 0;
}

uintptr_t Hook::get_trampoline() const {
    if (!is_valid()) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    auto it = g_hooks.find(target_address_);
    if (it == g_hooks.end()) {
        return 0;
    }
    auto& info = *it->second;
    std::lock_guard<std::mutex> info_lock(info.info_mutex);
    return reinterpret_cast<uintptr_t>(info.trampoline);
}

void Hook::reset() {
    target_address_ = 0;
    callback_ = nullptr;
    original_func_ = nullptr;
    is_enabled_ = false;
}

} // namespace ur::inline_hook
