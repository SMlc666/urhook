#include "ur/plthook.h"
#include "ur/memory.h"
#include "ur/maps_parser.h"
#include "ur/elf_parser.h"

#include <sys/mman.h>
#include <cstring>
#include <vector>
#include <algorithm>

namespace ur::plthook {

namespace {
constexpr uint32_t R_AARCH64_JUMP_SLOT_VAL = 1026;

int perms_to_prot(const std::string& perms) {
    int prot = 0;
    if (!perms.empty()) {
        if (perms.size() > 0 && perms[0] == 'r') prot |= PROT_READ;
        if (perms.size() > 1 && perms[1] == 'w') prot |= PROT_WRITE;
        if (perms.size() > 2 && perms[2] == 'x') prot |= PROT_EXEC;
    }
    return prot;
}
} // anonymous namespace

Hook::Hook(uintptr_t base_address)
    : base_(base_address) {
    if (base_ != 0) {
        elf_ = std::make_unique<elf_parser::ElfParser>(base_);
        parsed_ = elf_->parse();
    } else {
        parsed_ = false;
    }
}

Hook::Hook(const std::string& so_path) {
    // 尝试通过路径子串匹配找到最小起始地址作为该 so 的基址
    auto maps = maps_parser::MapsParser::parse();
    uintptr_t chosen_base = 0;
    for (const auto& m : maps) {
        const auto& p = m.get_path();
        if (!p.empty() && p.find(so_path) != std::string::npos) {
            if (chosen_base == 0 || m.get_start() < chosen_base) {
                chosen_base = m.get_start();
            }
        }
    }
    base_ = chosen_base;
    if (base_ != 0) {
        elf_ = std::make_unique<elf_parser::ElfParser>(base_);
        parsed_ = elf_->parse();
    } else {
        parsed_ = false;
    }
}

Hook::~Hook() {
    std::lock_guard<std::mutex> lock(mutex_);
    // 复制键，避免在迭代中修改容器
    std::vector<std::string> symbols;
    symbols.reserve(entries_.size());
    for (const auto& kv : entries_) symbols.push_back(kv.first);
    for (const auto& s : symbols) {
        // 尽力恢复，即使失败也继续其他项
        unhook_symbol(s);
    }
}

bool Hook::is_valid() const {
    return parsed_;
}

const Hook::Entry* Hook::get_entry(const std::string& symbol) const {
    auto it = entries_.find(symbol);
    if (it == entries_.end()) return nullptr;
    return &it->second;
}

bool Hook::parse_elf() {
    if (!elf_) {
        if (base_ == 0) return false;
        elf_ = std::make_unique<elf_parser::ElfParser>(base_);
    }
    if (!parsed_) {
        parsed_ = elf_->parse();
    }
    return parsed_;
}

bool Hook::write_got(uintptr_t got_addr, void* value, void** previous) {
    // 读取旧值
    void* oldval = nullptr;
    (void)ur::memory::read(got_addr, &oldval, sizeof(oldval));
    if (previous) {
        *previous = oldval;
    }

    // 记录原始权限
    ur::memory::MappedRegion region;
    int restore_prot = PROT_READ; // 默认只读
    if (ur::memory::find_mapped_region(got_addr, region)) {
        restore_prot = perms_to_prot(region.perms);
        if (restore_prot == 0) {
            // fallback
            restore_prot = PROT_READ;
        }
    }

    // 切换为可写
    if (!ur::memory::protect(got_addr, sizeof(void*), PROT_READ | PROT_WRITE)) {
        return false;
    }

    // 写入新值
    if (!ur::memory::write(got_addr, &value, sizeof(value))) {
        // 尝试恢复权限后返回失败
        (void)ur::memory::protect(got_addr, sizeof(void*), restore_prot);
        return false;
    }

    // 恢复原权限（失败不影响写入结果）
    (void)ur::memory::protect(got_addr, sizeof(void*), restore_prot);
    return true;
}

bool Hook::hook_symbol(const std::string& symbol, void* replacement, void** original_out) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!parse_elf()) return false;
    if (symbol.empty() || replacement == nullptr) return false;

    // 已存在则覆盖 replacement，返回原始指针
    auto it_existing = entries_.find(symbol);
    if (it_existing != entries_.end()) {
        // 将 GOT 再次写为新的 replacement
        if (!write_got(it_existing->second.got_addr, replacement, nullptr)) {
            return false;
        }
        it_existing->second.replacement = replacement;
        if (original_out) *original_out = it_existing->second.original;
        return true;
    }

    const Elf64_Sym* dynsym = elf_->get_dynamic_symbol_table();
    const char* dynstr = elf_->get_dynamic_string_table();
    if (dynsym == nullptr || dynstr == nullptr) return false;

    const uintptr_t rel_loc = elf_->get_plt_rel_location();
    const size_t rel_sz = elf_->get_plt_rel_size();
    const int rel_type = elf_->get_plt_rel_entry_type(); // DT_REL 或 DT_RELA
    if (rel_loc == 0 || rel_sz == 0) return false;

    // ARM64 常见为 DT_RELA，但也兼容 DT_REL
    if (rel_type == DT_RELA) {
        const auto* rela = reinterpret_cast<const Elf64_Rela*>(rel_loc);
        const size_t count = rel_sz / sizeof(Elf64_Rela);
        for (size_t i = 0; i < count; ++i) {
            const auto& r = rela[i];
            const uint32_t rtype = ELF64_R_TYPE(r.r_info);
            if (rtype != R_AARCH64_JUMP_SLOT_VAL) continue;

            const uint32_t sym_index = ELF64_R_SYM(r.r_info);
            const Elf64_Sym* s = &dynsym[sym_index];
            const char* name = dynstr + s->st_name;
            if (name && symbol == name) {
                const uintptr_t got_addr = r.r_offset; // 目标 GOT 条目地址（内存绝对地址）
                void* original = nullptr;
                if (!write_got(got_addr, replacement, &original)) {
                    return false;
                }
                Entry e;
                e.symbol = symbol;
                e.got_addr = got_addr;
                e.original = original;
                e.replacement = replacement;
                entries_.emplace(symbol, e);
                if (original_out) *original_out = original;
                return true;
            }
        }
    } else if (rel_type == DT_REL) {
        const auto* rel = reinterpret_cast<const Elf64_Rel*>(rel_loc);
        const size_t count = rel_sz / sizeof(Elf64_Rel);
        for (size_t i = 0; i < count; ++i) {
            const auto& r = rel[i];
            const uint32_t rtype = ELF64_R_TYPE(r.r_info);
            if (rtype != R_AARCH64_JUMP_SLOT_VAL) continue;

            const uint32_t sym_index = ELF64_R_SYM(r.r_info);
            const Elf64_Sym* s = &dynsym[sym_index];
            const char* name = dynstr + s->st_name;
            if (name && symbol == name) {
                const uintptr_t got_addr = r.r_offset; // 目标 GOT 条目地址（内存绝对地址）
                void* original = nullptr;
                if (!write_got(got_addr, replacement, &original)) {
                    return false;
                }
                Entry e;
                e.symbol = symbol;
                e.got_addr = got_addr;
                e.original = original;
                e.replacement = replacement;
                entries_.emplace(symbol, e);
                if (original_out) *original_out = original;
                return true;
            }
        }
    } else {
        // 未知类型
        return false;
    }

    // 未找到匹配符号
    return false;
}

bool Hook::unhook_symbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = entries_.find(symbol);
    if (it == entries_.end()) return false;

    // 写回原始指针
    if (!write_got(it->second.got_addr, it->second.original, nullptr)) {
        return false;
    }

    entries_.erase(it);
    return true;
}

} // namespace ur::plthook