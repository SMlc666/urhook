#pragma once

#include <cstdint>
#include <string>
#include <sys/uio.h> // For process_vm_writev

namespace ur {
    namespace memory {
        struct MappedRegion {
            uintptr_t start = 0;
            uintptr_t end = 0;
            uintptr_t offset = 0;
            std::string perms;
            std::string path;
        };

        // Finds the memory mapped region for a given address.
        bool find_mapped_region(uintptr_t address, MappedRegion& region);

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
