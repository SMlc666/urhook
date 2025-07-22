#pragma once

#include <cstdint>
#include <string_view>
#include <sys/uio.h> // For process_vm_writev

namespace ur {
    namespace memory {
        // 读取内存
        bool read(uintptr_t address, void* buffer, size_t size);

        // 写入内存
        bool write(uintptr_t address, const void* buffer, size_t size);

        // 修改内存保护
        bool protect(uintptr_t address, size_t size, int prot);

        // 刷新指令缓存
        void flush_instruction_cache(uintptr_t address, size_t size);
    }
}
