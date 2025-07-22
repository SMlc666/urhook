#pragma once

#include <vector>
#include <utility>
#include <sys/mman.h>
#include <unistd.h>
#include "ur/assembler.h"
#include "ur/memory.h"

namespace ur::jit {
    class Jit : public ur::assembler::Assembler {
    public:
        explicit Jit(uintptr_t address = 0);
        ~Jit() override;

        Jit(Jit&& other) noexcept ;
        Jit& operator=(Jit&& other) noexcept;


        template<typename T>
        T finalize(uintptr_t hint = 0) {
            const auto& code = get_code();
            auto size = get_code_size();
            if (size == 0) {
                return nullptr;
            }

            long page_size = sysconf(_SC_PAGESIZE);
            size_t aligned_size = (size + page_size - 1) & -page_size;

            mem_ = mmap(reinterpret_cast<void*>(hint), aligned_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            if (mem_ == MAP_FAILED) {
                return nullptr;
            }
            size_ = aligned_size;

            memcpy(mem_, code.data(), size);
            __builtin___clear_cache(reinterpret_cast<char*>(mem_), reinterpret_cast<char*>(mem_) + size);

            return reinterpret_cast<T>(mem_);
        }

        void* release();

    private:
        void* mem_{nullptr};
        size_t size_{0};
    };
}
