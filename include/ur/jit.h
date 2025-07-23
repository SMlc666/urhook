#pragma once

#include <vector>
#include <utility>
#include <sys/mman.h>
#include <unistd.h>
#include <map>
#include "ur/assembler.h"
#include "ur/memory.h"

namespace ur::jit {

    class Label {
    public:
        Label() : id_(next_id_++) {}
        bool is_bound() const { return bound_; }
        int32_t offset() const { return offset_; }

    private:
        friend class Jit;
        static uint32_t next_id_;
        uint32_t id_;
        bool bound_{false};
        int32_t offset_{-1};
        std::vector<uint32_t> references_; // Instruction offsets that refer to this label
    };


    class Jit : public ur::assembler::Assembler {
    public:
        explicit Jit(uintptr_t address = 0);
        ~Jit() override;

        Jit(Jit&& other) noexcept ;
        Jit& operator=(Jit&& other) noexcept;

        // Label and Branching support
        Label new_label();
        void bind(Label& label);

        using ur::assembler::Assembler::b;

        // Unconditional branches
        void b(Label& label);

        // Conditional branches
        void b(assembler::Condition cond, Label& label);
        void b_eq(Label& label) { b(assembler::Condition::EQ, label); }
        void b_ne(Label& label) { b(assembler::Condition::NE, label); }
        void b_cs(Label& label) { b(assembler::Condition::CS, label); }
        void b_hs(Label& label) { b(assembler::Condition::HS, label); }
        void b_cc(Label& label) { b(assembler::Condition::CC, label); }
        void b_lo(Label& label) { b(assembler::Condition::LO, label); }
        void b_mi(Label& label) { b(assembler::Condition::MI, label); }
        void b_pl(Label& label) { b(assembler::Condition::PL, label); }
        void b_vs(Label& label) { b(assembler::Condition::VS, label); }
        void b_vc(Label& label) { b(assembler::Condition::VC, label); }
        void b_hi(Label& label) { b(assembler::Condition::HI, label); }
        void b_ls(Label& label) { b(assembler::Condition::LS, label); }
        void b_ge(Label& label) { b(assembler::Condition::GE, label); }
        void b_lt(Label& label) { b(assembler::Condition::LT, label); }
        void b_gt(Label& label) { b(assembler::Condition::GT, label); }
        void b_le(Label& label) { b(assembler::Condition::LE, label); }


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
        std::map<uint32_t, Label*> label_patches_;
    };
}