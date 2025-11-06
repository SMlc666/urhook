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

    // Trampoline that holds relocated original instructions and jumps back
    void* trampoline = nullptr;
    size_t backup_size = 0;

    // Minimal patch strategy: detour stub and target patch information
    void* detour_stub = nullptr;             // Near stub that always receives the target jump
    size_t detour_stub_size = 0;             // Size of stub code (bytes)
    size_t patch_size_at_target = 0;         // Size of the chosen patch sequence at target (bytes)
    std::vector<uint32_t> target_patch_code; // Cached patch sequence words (uint32_t instructions)

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

 // Hint-based executable memory allocation (single attempt, no maps scanning)
void* allocate_executable_memory_hint(size_t size, uintptr_t hint) {
    long page_size = sysconf(_SC_PAGESIZE);
    size_t aligned_size = (size + page_size - 1) & -page_size;
    uintptr_t aligned_hint = hint ? (hint & ~(static_cast<uintptr_t>(page_size) - 1)) : 0;
    void* addr_hint = aligned_hint ? reinterpret_cast<void*>(aligned_hint) : nullptr;
    void* mem = mmap(addr_hint, aligned_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    return mem == MAP_FAILED ? nullptr : mem;
}

// Bounded near-allocation without maps scanning, tries symmetric hints around target.
// If MAP_FIXED_NOREPLACE is available, it will be used to request exact placement safely.
// Otherwise, it uses hints and validates the returned address is within max_distance;
// if not, it unmaps and continues.
void* allocate_executable_memory_near(uintptr_t target, size_t size, size_t max_distance) {
    long page_size = sysconf(_SC_PAGESIZE);
    size_t aligned_size = (size + page_size - 1) & -page_size;

    uintptr_t base = target & ~(static_cast<uintptr_t>(page_size) - 1);

    // Probe parameters: 1MB step, up to 256 symmetric probes (~256MB span).
    constexpr uintptr_t kStep = 1ull << 20; // 1MB
    const size_t max_probes = std::min<size_t>(256, (max_distance / kStep) + 1);

    auto within_window = [&](uintptr_t addr) -> bool {
        uintptr_t a = addr;
        uintptr_t t = target;
        uintptr_t dist = (a > t) ? (a - t) : (t - a);
        return dist <= max_distance;
    };

    for (size_t i = 0; i < max_probes; ++i) {
        for (int dir = (i == 0 ? 0 : -1); dir <= 1; dir += 2) {
            uintptr_t offset = i * kStep;
            uintptr_t candidate = base;
            if (dir < 0) {
                if (base >= offset) candidate = base - offset;
                else continue;
            } else if (dir > 0) {
                candidate = base + offset;
            } else { // i == 0
                candidate = base;
            }

            void* addr = reinterpret_cast<void*>(candidate);
            void* mem = nullptr;

#ifdef MAP_FIXED_NOREPLACE
            mem = mmap(addr, aligned_size,
                       PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE,
                       -1, 0);
            if (mem != MAP_FAILED) {
                // Exact placement succeeded; ensure in window then return.
                if (within_window(reinterpret_cast<uintptr_t>(mem))) {
                    return mem;
                } else {
                    munmap(mem, aligned_size);
                }
            }
#else
            // Use hint; kernel may place elsewhere. Validate window on success.
            mem = mmap(addr, aligned_size,
                       PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_ANONYMOUS | MAP_PRIVATE,
                       -1, 0);
            if (mem != MAP_FAILED) {
                if (within_window(reinterpret_cast<uintptr_t>(mem))) {
                    return mem;
                } else {
                    munmap(mem, aligned_size);
                }
            }
#endif
        }
    }

    return nullptr;
}

// Patch target with provided machine code (uint32 words)
bool patch_target_with_code(uintptr_t target, const std::vector<uint32_t>& code_words) {
    const auto* patch_code = reinterpret_cast<const uint8_t*>(code_words.data());
    size_t patch_size = code_words.size() * sizeof(uint32_t);
    return memory::atomic_patch(target, patch_code, patch_size);
}

// Choose shortest feasible patch sequence from target -> dest
// Priority: B (4B) → ADRP+ADD+BR (12B) → ABS jump (20B)
bool choose_patch_sequence(uintptr_t target, uintptr_t dest, std::vector<uint32_t>& out_code, size_t& out_patch_size) {
    using namespace ur::assembler;
    // Try single B within ±128MB
    try {
        Assembler asm_b(target);
        asm_b.b(dest);
        out_code = asm_b.get_code();
        out_patch_size = asm_b.get_code_size();
        return true;
    } catch (const std::runtime_error&) {
        // fallthrough
    }

    // Try ADRP + ADD + BR within ADRP page window
    try {
        Assembler asm_adrp(target);
        asm_adrp.adrp(Register::X16, dest);
        asm_adrp.add(Register::X16, Register::X16, static_cast<uint16_t>(dest & 0xFFF), false);
        asm_adrp.br(Register::X16);
        out_code = asm_adrp.get_code();
        out_patch_size = asm_adrp.get_code_size();
        return true;
    } catch (const std::runtime_error&) {
        // fallthrough
    }

    // Fallback to ABS jump (MOVZ/MOVK×4 + BR)
    Assembler asm_abs(target);
    asm_abs.gen_abs_jump(dest, Register::X16);
    out_code = asm_abs.get_code();
    out_patch_size = asm_abs.get_code_size();
    return true;
}

// Build or update detour stub: absolute jump to detour_addr
bool update_detour_stub(HookInfo& info, uintptr_t detour_addr) {
    if (!info.detour_stub) return false;
    using namespace ur::assembler;
    Assembler stub_asm(reinterpret_cast<uintptr_t>(info.detour_stub));
    stub_asm.gen_abs_jump(detour_addr, Register::X16);
    const auto& stub_code = stub_asm.get_code();

    memcpy(info.detour_stub, stub_code.data(), stub_code.size() * sizeof(uint32_t));
    info.detour_stub_size = stub_asm.get_code_size();

    __builtin___clear_cache(reinterpret_cast<char*>(info.detour_stub),
                            reinterpret_cast<char*>(info.detour_stub) + info.detour_stub_size);
    return true;
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
std::vector<uint32_t> relocate_trampoline(uintptr_t target, uintptr_t trampoline_addr, size_t& backup_size, size_t required_size) {
    auto disassembler = disassembler::CreateAArch64Disassembler();
    const uint8_t* code = reinterpret_cast<const uint8_t*>(target);
    
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
                // Other PC-relative.
                // Handle ADR as absolute load to avoid PC-relative issues.
                if (current_insn.id == disassembler::InstructionId::ADR && current_insn.operands.size() > 1 &&
                    current_insn.operands[0].type == disassembler::OperandType::REGISTER &&
                    current_insn.operands[1].type == disassembler::OperandType::IMMEDIATE) {
                    auto dest_reg = std::get<assembler::Register>(current_insn.operands[0].value);
                    uintptr_t target_addr = std::get<int64_t>(current_insn.operands[1].value);
                    tramp_asm.gen_load_address(dest_reg, target_addr);
                    backup_size += current_insn.size;
                } else {
                    // Fallback: copy as-is (potentially unsafe). TODO: add more PC-relative rewrites if needed.
                    uint32_t instruction_word = *reinterpret_cast<const uint32_t*>(current_insn.bytes.data());
                    tramp_asm.get_code_mut().push_back(instruction_word);
                    backup_size += current_insn.size;
                }
            }
        } else {
            // Not a PC-relative instruction.
            // Special handling: If this is the second instruction of an ADRP pair (hook starts at ADD/LDR/STR),
            // reconstruct absolute address to avoid relying on the previous ADRP side-effect.
            bool handled = false;

            if (i == 0) {
                // Try to decode the previous instruction (at target - 4).
                auto prev_vec = disassembler->Disassemble(target - 4, reinterpret_cast<const uint8_t*>(target - 4), 4, 1);
                if (!prev_vec.empty()) {
                    auto& prev = prev_vec[0];
                    if (prev.id == disassembler::InstructionId::ADRP) {
                        auto adrp_dest_reg = std::get<assembler::Register>(prev.operands[0].value);
                        uintptr_t page_addr = std::get<int64_t>(prev.operands[1].value);

                        // Case A: ADD following ADRP: ADD Xd, Xn(=ADRP rd), #imm{, lsl #12}
                        if (current_insn.id == disassembler::InstructionId::ADD &&
                            current_insn.operands.size() > 2 &&
                            current_insn.operands[1].type == disassembler::OperandType::REGISTER &&
                            std::get<assembler::Register>(current_insn.operands[1].value) == adrp_dest_reg &&
                            current_insn.operands[2].type == disassembler::OperandType::IMMEDIATE) {

                            uintptr_t final_addr = page_addr + std::get<int64_t>(current_insn.operands[2].value);
                            auto dest_reg = std::get<assembler::Register>(current_insn.operands[0].value);
                            tramp_asm.gen_load_address(dest_reg, final_addr);
                            backup_size += current_insn.size;
                            handled = true;
                        }
                        // Case B: LDR/STR with base from ADRP: LDR/STR Rt, [Xn(=ADRP rd), #disp]
                        else if ((current_insn.id == disassembler::InstructionId::LDR || current_insn.id == disassembler::InstructionId::STR) &&
                                 current_insn.operands.size() > 1 &&
                                 current_insn.operands[1].type == disassembler::OperandType::MEMORY) {

                            auto mem_op = std::get<disassembler::MemOperand>(current_insn.operands[1].value);
                            if (mem_op.base == adrp_dest_reg) {
                                uintptr_t final_addr = page_addr + mem_op.displacement;
                                auto data_reg = std::get<assembler::Register>(current_insn.operands[0].value);

                                tramp_asm.gen_load_address(assembler::Register::X16, final_addr);
                                if (current_insn.id == disassembler::InstructionId::LDR) {
                                    tramp_asm.ldr(data_reg, assembler::Register::X16, 0);
                                } else { // STR
                                    tramp_asm.str(data_reg, assembler::Register::X16, 0);
                                }
                                backup_size += current_insn.size;
                                handled = true;
                            }
                        }
                    }
                }
            }

            if (!handled) {
                // Default: copy the instruction word as-is.
                uint32_t instruction_word = *reinterpret_cast<const uint32_t*>(current_insn.bytes.data());
                tramp_asm.get_code_mut().push_back(instruction_word);
                backup_size += current_insn.size;
            }
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

        // Allocate detour stub once (bounded near attempts; no maps scanning)
        if (info.detour_stub == nullptr) {
            constexpr size_t kNearWindow = 128ull * 1024 * 1024; // 128MB
            info.detour_stub = allocate_executable_memory_near(target, assembler::Assembler::ABS_JUMP_SIZE, kNearWindow);
            if (!info.detour_stub) {
                // Fallback: one-shot hint, then generic mapping
                info.detour_stub = allocate_executable_memory_hint(assembler::Assembler::ABS_JUMP_SIZE, target);
                if (!info.detour_stub) {
                    info.detour_stub = allocate_executable_memory(assembler::Assembler::ABS_JUMP_SIZE);
                }
            }
        }

        // Choose minimal patch sequence from target to detour stub (cache code and patch size)
        if (info.target_patch_code.empty() || info.patch_size_at_target == 0) {
            if (info.detour_stub) {
                std::vector<uint32_t> patch_code;
                size_t patch_size = 0;
                choose_patch_sequence(target, reinterpret_cast<uintptr_t>(info.detour_stub), patch_code, patch_size);
                info.target_patch_code = std::move(patch_code);
                info.patch_size_at_target = patch_size;
            } else {
                // No stub: patch size equals ABS jump; target_patch_code left empty to force direct patch
                info.patch_size_at_target = assembler::Assembler::ABS_JUMP_SIZE;
            }
        }

        // Build trampoline once
        if (info.trampoline == nullptr) {
            // Relocate with dynamic required size equal to selected patch size
            auto relocated_code = relocate_trampoline(target, 0, info.backup_size, info.patch_size_at_target);
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
            
            __builtin___clear_cache(reinterpret_cast<char*>(info.trampoline),
                                    reinterpret_cast<char*>(info.trampoline) + trampoline_size);
        }

        // Build detour stub to current detour target (callback when enabling now, otherwise trampoline)
        if (info.detour_stub) {
            uintptr_t detour_target = enable_now ? reinterpret_cast<uintptr_t>(callback)
                                                 : reinterpret_cast<uintptr_t>(info.trampoline);
            update_detour_stub(info, detour_target);
        }

        void* next_func_to_call = info.entries.empty() ? info.trampoline : info.entries.front().callback;
        original_func_ = next_func_to_call;

        info.entries.push_front({this, callback, next_func_to_call, enable_now});

        if (enable_now) {
            if (info.detour_stub && !info.target_patch_code.empty()) {
                // Patch target to detour stub using minimal sequence
                patch_target_with_code(target, info.target_patch_code);
            } else {
                // Fallback: direct absolute jump to callback
                patch_target(target, reinterpret_cast<uintptr_t>(callback));
            }
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
        
        // If this is the currently active hook, route to the new detour
        if (is_enabled_ && info.entries.front().owner == this) {
            if (info.detour_stub) {
                update_detour_stub(info, reinterpret_cast<uintptr_t>(callback));
                // Ensure target points to stub with minimal patch
                if (!info.target_patch_code.empty()) {
                    patch_target_with_code(target_address_, info.target_patch_code);
                } else {
                    // Fallback to absolute jump to detour when patch code missing
                    patch_target(target_address_, reinterpret_cast<uintptr_t>(callback));
                }
            } else {
                // No stub: direct patch to callback
                patch_target(target_address_, reinterpret_cast<uintptr_t>(callback));
            }
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
        if (info.detour_stub) {
            free_executable_memory(info.detour_stub, info.detour_stub_size ? info.detour_stub_size : assembler::Assembler::ABS_JUMP_SIZE);
        }
        g_hooks.erase(it);
    } else {
        // Keep target patch routing
        if (info.detour_stub) {
            // Update stub to point to current chain head
            update_detour_stub(info, reinterpret_cast<uintptr_t>(info.entries.front().callback));
            if (!info.target_patch_code.empty()) {
                patch_target_with_code(target_address_, info.target_patch_code);
            } else {
                // If patch code missing, ensure direct jump to current chain head
                patch_target(target_address_, reinterpret_cast<uintptr_t>(info.entries.front().callback));
            }
        } else {
            // No stub: direct patch to current chain head
            patch_target(target_address_, reinterpret_cast<uintptr_t>(info.entries.front().callback));
        }
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
        if (info.detour_stub) {
            update_detour_stub(info, reinterpret_cast<uintptr_t>(first_enabled->callback));
            if (!info.target_patch_code.empty()) {
                patch_target_with_code(target_address_, info.target_patch_code);
            } else {
                // If minimal patch code not cached, ensure direct jump
                patch_target(target_address_, reinterpret_cast<uintptr_t>(first_enabled->callback));
            }
        } else {
            // No stub: direct patch to first enabled callback
            patch_target(target_address_, reinterpret_cast<uintptr_t>(first_enabled->callback));
        }
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
        if (info.detour_stub) {
            update_detour_stub(info, reinterpret_cast<uintptr_t>(first_enabled->callback));
            if (!info.target_patch_code.empty()) {
                patch_target_with_code(target_address_, info.target_patch_code);
            } else {
                patch_target(target_address_, reinterpret_cast<uintptr_t>(first_enabled->callback));
            }
        } else {
            // No stub: direct patch to first enabled callback
            patch_target(target_address_, reinterpret_cast<uintptr_t>(first_enabled->callback));
        }
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
