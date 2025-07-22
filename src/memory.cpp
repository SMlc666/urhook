#include "ur/memory.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <sys/uio.h> // For process_vm_writev
#include <fstream>
#include <sstream>

namespace ur {
    namespace memory {
        bool read(uintptr_t address, void* buffer, size_t size) {
            memcpy(buffer, reinterpret_cast<const void*>(address), size);
            return true;
        }

        bool write(uintptr_t address, const void* buffer, size_t size) {
            // 之前已经通过 protect 设置了可写权限，直接用 memcpy 即可
            memcpy(reinterpret_cast<void*>(address), buffer, size);
            return true;
        }

        bool protect(uintptr_t address, size_t size, int prot) {
            long page_size = sysconf(_SC_PAGESIZE);
            
            uintptr_t page_start = address & -page_size;
            
            // 计算需要保护的总长度，并向上对齐到页大小的整数倍
            size_t total_size = size + (address - page_start);
            size_t aligned_size = (total_size + page_size - 1) & -page_size;

            return mprotect(reinterpret_cast<void*>(page_start), aligned_size, prot) == 0;
        }

        void flush_instruction_cache(uintptr_t address, size_t size) {
            char* start = reinterpret_cast<char*>(address);
            char* end = start + size;

            // Data Synchronization Barrier
            // Ensures that all previous memory accesses (writes) are complete
            // before we proceed with the instruction cache invalidation.
            asm volatile("dsb ish" : : : "memory");

            // The cache line size on modern ARM64 is typically 64 bytes.
            // We iterate and invalidate by this size for efficiency.
            long cache_line_size = sysconf(_SC_LEVEL1_ICACHE_LINESIZE);
            if (cache_line_size <= 0) {
                cache_line_size = 64; // A reasonable default
            }
            for (char* p = start; p < end; p += cache_line_size) {
                asm volatile("ic ivau, %0" : : "r"(p) : "memory");
            }

            // Data Synchronization Barrier again to ensure the cache invalidation is complete
            // before any new instructions are fetched.
            asm volatile("dsb ish" : : : "memory");

            // Instruction Synchronization Barrier
            // Flushes the pipeline and ensures that all subsequent instructions
            // are fetched from the newly cleaned cache.
            asm volatile("isb" : : : "memory");
        }

        bool find_mapped_region(uintptr_t address, MappedRegion& region) {
            std::ifstream maps_file("/proc/self/maps");
            std::string line;
            if (!maps_file.is_open()) {
                return false;
            }

            while (std::getline(maps_file, line)) {
                std::istringstream iss(line);
                uintptr_t start, end, offset;
                char dash, colon;
                std::string perms, dev;
                long inode;
                std::string path;

                iss >> std::hex >> start >> dash >> end >> perms >> offset >> std::dec >> dev >> inode;
                // The rest of the line is the path, which might contain spaces
                std::getline(iss, path);
                // Trim leading whitespace from path
                path.erase(0, path.find_first_not_of(" \t"));


                if (address >= start && address < end) {
                    region.start = start;
                    region.end = end;
                    region.offset = offset;
                    region.perms = perms;
                    region.path = path;
                    maps_file.close();
                    return true;
                }
            }

            maps_file.close();
            return false;
        }
    }
}
