#include "ur/inline_hook.h"
#include "ur/memory.h"
#include "ur/assembler.h"
#include "ur/thread.h"

#include <capstone/capstone.h>
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

// RAII wrapper for Capstone.
class CapstoneHandle {
public:
    CapstoneHandle() {
        if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &handle_) != CS_ERR_OK) {
            throw std::runtime_error("Failed to initialize Capstone");
        }
        cs_option(handle_, CS_OPT_DETAIL, CS_OPT_ON);
    }
    ~CapstoneHandle() {
        if (handle_ != 0) cs_close(&handle_);
    }
    csh get() const { return handle_; }
    CapstoneHandle(const CapstoneHandle&) = delete;
    CapstoneHandle& operator=(const CapstoneHandle&) = delete;
private:
    csh handle_ = 0;
};

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
    const auto& patch_code = assembler.get_code();
    size_t patch_size = patch_code.size() * sizeof(uint32_t);

    memory::protect(target, patch_size, PROT_READ | PROT_WRITE | PROT_EXEC);
    bool result = memory::write(target, patch_code.data(), patch_size);
    memory::protect(target, patch_size, PROT_READ | PROT_EXEC);
    
    __builtin___clear_cache(reinterpret_cast<char*>(target), reinterpret_cast<char*>(target + patch_size));
    return result;
}

bool restore_target(HookInfo& info) {
    memory::protect(info.target_address, info.backup_size, PROT_READ | PROT_WRITE | PROT_EXEC);
    bool result = memory::write(info.target_address, info.original_code.data(), info.backup_size);
    memory::protect(info.target_address, info.backup_size, PROT_READ | PROT_EXEC);

    __builtin___clear_cache(reinterpret_cast<char*>(info.target_address), reinterpret_cast<char*>(info.target_address + info.backup_size));
    return result;
}

// --- Trampoline Relocation Logic ---

// Relocates instructions from the target function to a trampoline.
// Returns the relocated machine code.
std::vector<uint32_t> relocate_trampoline(uintptr_t target, uintptr_t trampoline_addr, size_t& backup_size) {
    CapstoneHandle cs;
    cs_insn* insn;
    const uint8_t* code = reinterpret_cast<const uint8_t*>(target);
    const size_t required_size = assembler::Assembler::ABS_JUMP_SIZE;
    
    size_t count = cs_disasm(cs.get(), code, 100, target, 0, &insn);
    if (count == 0) throw std::runtime_error("Failed to disassemble target function");

    assembler::Assembler tramp_asm(trampoline_addr);
    backup_size = 0;

    for (size_t i = 0; i < count; ++i) {
        cs_insn& current_insn = insn[i];
        cs_detail* detail = current_insn.detail;

        // Handle ADRP instruction
        if (current_insn.id == ARM64_INS_ADRP) {
            bool pair_relocated = false;
            uintptr_t page_addr = detail->arm64.operands[1].imm;

            if (i + 1 < count) {
                cs_insn& next_insn = insn[i + 1];
                cs_detail* next_detail = next_insn.detail;
                auto adrp_dest_reg = detail->arm64.operands[0].reg;
                uintptr_t final_addr;

                // Case 1: ADRP + ADD
                if (next_insn.id == ARM64_INS_ADD && next_detail->arm64.op_count > 2 &&
                    next_detail->arm64.operands[1].type == ARM64_OP_REG && next_detail->arm64.operands[1].reg == adrp_dest_reg &&
                    next_detail->arm64.operands[2].type == ARM64_OP_IMM) {

                    final_addr = page_addr + next_detail->arm64.operands[2].imm;
                    auto dest_reg = static_cast<assembler::Register>(next_detail->arm64.operands[0].reg - ARM64_REG_X0);
                    tramp_asm.gen_load_address(dest_reg, final_addr);
                    pair_relocated = true;
                }
                // Case 2: ADRP + LDR/STR (memory access)
                else if ((next_insn.id == ARM64_INS_LDR || next_insn.id == ARM64_INS_STR) && next_detail->arm64.op_count > 1 &&
                         next_detail->arm64.operands[1].type == ARM64_OP_MEM && next_detail->arm64.operands[1].mem.base == adrp_dest_reg) {

                    final_addr = page_addr + next_detail->arm64.operands[1].mem.disp;
                    auto data_reg = static_cast<assembler::Register>(next_detail->arm64.operands[0].reg - ARM64_REG_X0);

                    tramp_asm.gen_load_address(assembler::Register::X16, final_addr);
                    if (next_insn.id == ARM64_INS_LDR) {
                        tramp_asm.ldr(data_reg, assembler::Register::X16, 0);
                    } else { // STR
                        tramp_asm.str(data_reg, assembler::Register::X16, 0);
                    }
                    pair_relocated = true;
                }
            }

            if (pair_relocated) {
                backup_size += current_insn.size + insn[i+1].size;
                i++; // Consumed two instructions
            } else {
                // If not a recognized pair or last instruction, just relocate the ADRP itself.
                auto dest_reg = static_cast<assembler::Register>(detail->arm64.operands[0].reg - ARM64_REG_X0);
                tramp_asm.gen_load_address(dest_reg, page_addr);
                backup_size += current_insn.size;
            }
        }
        // Handle LDR (literal)
        else if (current_insn.id == ARM64_INS_LDR && detail->arm64.op_count > 1 && detail->arm64.operands[1].type == ARM64_OP_IMM) {
             uintptr_t target_addr = detail->arm64.operands[1].imm;
             auto dest_reg = static_cast<assembler::Register>(detail->arm64.operands[0].reg - ARM64_REG_X0);
             tramp_asm.gen_load_address(assembler::Register::X16, target_addr);
             tramp_asm.ldr(dest_reg, assembler::Register::X16, 0);
             backup_size += current_insn.size;
        }
        // Handle branches
        else {
            bool is_branch = false;
            for (int j = 0; j < detail->groups_count; ++j) {
                if (detail->groups[j] == ARM64_GRP_JUMP || detail->groups[j] == ARM64_GRP_BRANCH_RELATIVE) {
                    is_branch = true;
                    break;
                }
            }

            if (is_branch) {
                uintptr_t target_addr = detail->arm64.operands[0].imm;

                if (current_insn.id == ARM64_INS_B && detail->arm64.cc != ARM64_CC_AL) {
                    auto cond = static_cast<assembler::Condition>(detail->arm64.cc);
                    tramp_asm.b(cond, target_addr);
                } else {
                    tramp_asm.gen_load_address(assembler::Register::X16, target_addr);
                    if (current_insn.id == ARM64_INS_BL) {
                        tramp_asm.blr(assembler::Register::X16);
                    } else {
                        tramp_asm.br(assembler::Register::X16);
                    }
                }
                backup_size += current_insn.size;
            } else {
                // Not a PC-relative instruction, just copy it
                uint32_t instruction_word = *reinterpret_cast<const uint32_t*>(current_insn.bytes);
                tramp_asm.get_code_mut().push_back(instruction_word);
                backup_size += current_insn.size;
            }
        }

        if (backup_size >= required_size) break;
    }

    cs_free(insn, count);
    return tramp_asm.get_code();
}


} // namespace

// --- Hook Class Implementation ---

Hook::Hook(uintptr_t target, Callback callback) {
    if (target == 0 || callback == nullptr) {
        throw std::invalid_argument("Target and callback must not be null");
    }

    thread::suspend_all_other_threads();
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

        if (info.entries.empty()) {
            original_func_ = info.trampoline;
        } else {
            original_func_ = info.entries.front().callback;
        }

        info.entries.push_front({this, callback, nullptr});
        
        patch_target(target, reinterpret_cast<uintptr_t>(callback));
    } catch (...) {
        thread::resume_all_other_threads();
        throw;
    }
    thread::resume_all_other_threads();
}

Hook::~Hook() {
    unhook();
}

void Hook::unhook() {
    if (!is_valid()) return;

    thread::suspend_all_other_threads();

    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    auto it = g_hooks.find(target_address_);
    if (it == g_hooks.end()) {
        reset();
        thread::resume_all_other_threads();
        return;
    }

    auto& info = *it->second;
    std::lock_guard<std::mutex> info_lock(info.info_mutex);

    auto entry_it = std::find_if(info.entries.begin(), info.entries.end(), 
        [this](const HookEntry& entry) { return entry.owner == this; });

    if (entry_it != info.entries.end()) {
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
    thread::resume_all_other_threads();
}

Hook::Hook(Hook&& other) noexcept
    : target_address_(other.target_address_),
      callback_(other.callback_),
      original_func_(other.original_func_) {
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
    return target_address_ != 0 && callback_ != nullptr && original_func_ != nullptr;
}

void Hook::reset() {
    target_address_ = 0;
    callback_ = nullptr;
    original_func_ = nullptr;
}

} // namespace ur::inline_hook