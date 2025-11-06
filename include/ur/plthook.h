#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <cstdint>

#include "ur/elf_parser.h"
#include "ur/maps_parser.h"
#include "ur/memory.h"

namespace ur::plthook {

class Hook {
public:
    explicit Hook(uintptr_t base_address);
    explicit Hook(const std::string& so_path);
    ~Hook();

    bool is_valid() const;

    // 安装符号 Hook：将符号的 GOT 指针替换为 replacement
    // original_out 输出原始函数指针（GOT 原值），可用于直接调用原始实现
    bool hook_symbol(const std::string& symbol, void* replacement, void** original_out);

    // 卸载指定符号 Hook，恢复 GOT 原始值
    bool unhook_symbol(const std::string& symbol);

    struct Entry {
        std::string symbol;
        uintptr_t got_addr = 0;
        void* original = nullptr;
        void* replacement = nullptr;
    };

    const Entry* get_entry(const std::string& symbol) const;

private:
    bool parse_elf();
    bool write_got(uintptr_t got_addr, void* value, void** previous);

    uintptr_t base_ = 0;
    std::unique_ptr<elf_parser::ElfParser> elf_;
    bool parsed_ = false;

    std::unordered_map<std::string, Entry> entries_;
    mutable std::mutex mutex_;
};

} // namespace ur::plthook