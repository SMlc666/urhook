#include <gtest/gtest.h>
#include <sys/mman.h>
#include <iostream>
#include <string>
#include "ur/jit.h"
#include "ur/assembler.h"
#include "ur/memory.h"
#include "ur/inline_hook.h"

// Forward declare functions from test_target_functions.cpp
namespace ur::test_target_functions {
    extern int hook_func();
    extern int original_target_func();
}

void print_hello_world() {
    std::cout << "Hello, World!" << std::endl;
}

TEST(JitTest, GenerateAndExecute) {
    ur::jit::Jit jit;
    jit.mov(ur::assembler::Register::W0, 42);
    jit.ret();

    auto func = jit.finalize<int(*)()>();
    ASSERT_NE(func, nullptr);

    EXPECT_EQ(func(), 42);
}

TEST(JitTest, JitAndHook) {
    ur::jit::Jit jit;
    jit.mov(ur::assembler::Register::W0, 100);
    jit.ret();

    auto original_func = jit.finalize<int(*)()>();
    ASSERT_NE(original_func, nullptr);
    ASSERT_EQ(original_func(), 100);

    {
        ur::inline_hook::Hook hook(reinterpret_cast<uintptr_t>(original_func), reinterpret_cast<ur::inline_hook::Hook::Callback>(ur::test_target_functions::hook_func));
        EXPECT_EQ(original_func(), 200);
    }

    EXPECT_EQ(original_func(), 100);
}

TEST(JitTest, JitAsDetour) {
    ur::jit::Jit jit;
    jit.mov(ur::assembler::Register::W0, 300);
    jit.ret();

    auto detour_func = jit.finalize<ur::inline_hook::Hook::Callback>();
    ASSERT_NE(detour_func, nullptr);
    ASSERT_EQ(ur::test_target_functions::original_target_func(), 50);

    {
        ur::inline_hook::Hook hook(reinterpret_cast<uintptr_t>(ur::test_target_functions::original_target_func), detour_func);
        EXPECT_EQ(ur::test_target_functions::original_target_func(), 300);
    }

    EXPECT_EQ(ur::test_target_functions::original_target_func(), 50);
}

TEST(JitTest, ReleaseMemory) {
    ur::jit::Jit jit;
    jit.mov(ur::assembler::Register::W0, 500);
    jit.ret();

    auto func = jit.finalize<int(*)()>();
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func(), 500);

    void* mem = jit.release();
    ASSERT_NE(mem, nullptr);

    // After release, the Jit object should not manage the memory anymore.
    // We can call the function again to ensure the memory is still valid.
    auto released_func = reinterpret_cast<int(*)()>(mem);
    EXPECT_EQ(released_func(), 500);

    // Manually free the memory
    munmap(mem, jit.get_code_size());
}

TEST(JitTest, HelloWorld) {
    ur::jit::Jit jit;

    // Standard function prologue to save FP and LR
    jit.stp(ur::assembler::Register::FP, ur::assembler::Register::LR, ur::assembler::Register::SP, -16, true);
    jit.mov(ur::assembler::Register::FP, ur::assembler::Register::SP);

    // Generate code to call the print_hello_world function
    // Use X16 (IP0) as it's a dedicated intra-procedure-call scratch register
    jit.gen_load_address(ur::assembler::Register::X16, reinterpret_cast<uintptr_t>(print_hello_world));
    jit.blr(ur::assembler::Register::X16);

    // Standard function epilogue to restore FP and LR
    jit.ldp(ur::assembler::Register::FP, ur::assembler::Register::LR, ur::assembler::Register::SP, 16, true);
    jit.ret();

    auto func = jit.finalize<void(*)()>();
    ASSERT_NE(func, nullptr);

    // Capture stdout
    testing::internal::CaptureStdout();
    func();
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_EQ(output, "Hello, World!\n");
}
