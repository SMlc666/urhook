#include "ur/jit.h"
#include <utility>

namespace ur::jit {
    Jit::Jit(uintptr_t address) : Assembler(address) {}

    Jit::~Jit() {
        if (mem_ != nullptr) {
            munmap(mem_, size_);
        }
    }

    Jit::Jit(Jit&& other) noexcept : Assembler(std::move(other)) {
        mem_ = other.mem_;
        size_ = other.size_;
        other.mem_ = nullptr;
        other.size_ = 0;
    }

    Jit& Jit::operator=(Jit&& other) noexcept {
        if (this != &other) {
            Assembler::operator=(std::move(other));
            if (mem_ != nullptr) {
                munmap(mem_, size_);
            }
            mem_ = other.mem_;
            size_ = other.size_;
            other.mem_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    void* Jit::release() {
        void* released_mem = mem_;
        mem_ = nullptr;
        size_ = 0;
        return released_mem;
    }
}
