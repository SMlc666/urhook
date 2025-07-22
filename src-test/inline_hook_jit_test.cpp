#include "ur/inline_hook.h"
#include "ur/jit.h"
#include <gtest/gtest.h>
#include <iostream>
#include <vector>
#include <string>

// A simple function to be JIT-compiled and hooked.
// It takes two integers and returns their sum.
using JitTargetFunc = int (*)(int, int);

// Global log for hook calls
static std::vector<std::string> g_jit_hook_call_log;
static ur::inline_hook::Hook* g_jit_hook = nullptr;

// Hook callback function
int jit_hook_callback(int a, int b) {
    g_jit_hook_call_log.push_back("JIT Hook called");
    std::cout << "  JIT Hook: Before calling original. Args: " << a << ", " << b << std::endl;
    int result = g_jit_hook->call_original<int>(a, b);
    std::cout << "  JIT Hook: After calling original. Result: " << result << std::endl;
    return result + 100;
}

class InlineHookJitTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_jit_hook_call_log.clear();
        g_jit_hook = nullptr;
    }

    void TearDown() override {
        g_jit_hook = nullptr;
    }
};

TEST_F(InlineHookJitTest, LongDistanceHook) {
    std::cout << "--- Running LongDistanceHook Test ---" << std::endl;

    // Define a target address far away from the current code segment.
    // This address should be chosen carefully to avoid conflicts with existing memory mappings.
    // For aarch64, addresses typically start from 0x4000000000 for user space.
    // We'll pick something high to ensure a large distance.
    uintptr_t far_address_hint = 0x7000000000; // A high address in user space

    // 1. JIT compile a simple function at the far address.
    ur::jit::Jit jit_compiler;
    // Function: int func(int a, int b) { return a + b; }
    // mov x0, x0 (a)
    // add x0, x0, x1 (a + b)
    // ret
    jit_compiler.add(ur::assembler::Register::X0, ur::assembler::Register::X0, ur::assembler::Register::X1); // x0 = x0 + x1
    jit_compiler.ret();

    JitTargetFunc jit_func = jit_compiler.finalize<JitTargetFunc>(far_address_hint);
    ASSERT_NE(jit_func, nullptr) << "Failed to JIT compile function at far address.";
    std::cout << "  JIT function compiled at: 0x" << std::hex << reinterpret_cast<uintptr_t>(jit_func) << std::dec << std::endl;

    // Verify the JIT compiled function works as expected before hooking
    EXPECT_EQ(jit_func(5, 7), 12);

    // 2. Hook the JIT compiled function within a scope to control its lifetime.
    {
        ur::inline_hook::Hook hook(
            reinterpret_cast<uintptr_t>(jit_func),
            reinterpret_cast<ur::inline_hook::Hook::Callback>(&jit_hook_callback)
        );
        g_jit_hook = &hook;
        ASSERT_TRUE(hook.is_valid());
        std::cout << "  Hook installed on JIT function." << std::endl;

        // 3. Call the hooked JIT function and verify.
        int result = jit_func(10, 20);
        EXPECT_EQ(result, (10 + 20) + 100); // Original result + hook modification
        ASSERT_EQ(g_jit_hook_call_log.size(), 1);
        EXPECT_EQ(g_jit_hook_call_log[0], "JIT Hook called");
        std::cout << "  Hooked JIT function called successfully. Result: " << result << std::endl;
    } // 'hook' object goes out of scope here, triggering unhook()

    // 4. Verify original behavior after hook has been uninstalled.
    g_jit_hook_call_log.clear(); // Clear log for next check
    EXPECT_EQ(jit_func(15, 25), 40); // Should be original behavior now
    ASSERT_TRUE(g_jit_hook_call_log.empty()); // No hook calls
    std::cout << "  Hook uninstalled (via destructor). Original JIT function behavior restored." << std::endl;

    // Release JIT memory
    jit_compiler.release();

    std::cout << "--- LongDistanceHook Test Finished ---" << std::endl;
}
