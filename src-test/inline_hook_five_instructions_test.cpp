#include "ur/inline_hook.h"
#include <gtest/gtest.h>
#include <iostream>
#include <vector>
#include <string>

// --- Global state for testing ---
static std::vector<std::string> g_five_ins_hook_log;
static ur::inline_hook::Hook* g_five_ins_hook_ptr = nullptr;

// --- Test Target Function ---
// A function written entirely in assembly with the `naked` attribute.
// The total number of instructions, including `ret`, is exactly 5.
// The logic is `(val + 5) * 2`, with the result being left in w0.
// Since the C++ signature is `void`, the result is discarded by the caller,
// which is fine for this test's purpose.
__attribute__((naked))
void target_five_instructions(int val) {
    asm volatile(
        "add w0, w0, #5\n"
        "mov w1, #2\n"
        "mul w0, w0, w1\n"
        "nop\n"
        "ret\n"
    );
}

// --- Hook Callback Function ---
void five_ins_hook_callback(int val) {
    g_five_ins_hook_log.push_back("Five-ins hook called");
    std::cout << "  Five-ins Hook: Before calling original. Arg: " << val << std::endl;
    g_five_ins_hook_ptr->call_original<void>(val);
    std::cout << "  Five-ins Hook: After calling original."
 << std::endl;
}

// --- Test Fixture ---
class InlineHookFiveInstructionsTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_five_ins_hook_log.clear();
        g_five_ins_hook_ptr = nullptr;
    }
};

// --- Test Case ---
TEST_F(InlineHookFiveInstructionsTest, HookFiveInstructionFunction) {
    std::cout << "--- Running HookFiveInstructionFunction Test ---" << std::endl;

    // 1. Call original function to ensure it doesn't crash
    target_five_instructions(20);

    {
        // 2. Hook the function within a scope to control its lifetime via RAII.
        ur::inline_hook::Hook hook(
            reinterpret_cast<uintptr_t>(&target_five_instructions),
            reinterpret_cast<ur::inline_hook::Hook::Callback>(&five_ins_hook_callback)
        );
        g_five_ins_hook_ptr = &hook;
        ASSERT_TRUE(hook.is_valid());
        std::cout << "  Hook installed on 5-instruction function."
 << std::endl;

        // 3. Call the hooked function and verify the hook was called via the log.
        g_five_ins_hook_log.clear();
        target_five_instructions(50);
        ASSERT_EQ(g_five_ins_hook_log.size(), 1);
        EXPECT_EQ(g_five_ins_hook_log[0], "Five-ins hook called");
        std::cout << "  Hooked function called successfully."
 << std::endl;
    }
    
    g_five_ins_hook_ptr = nullptr;
    std::cout << "  Hook uninstalled."
 << std::endl;

    // 4. Verify original behavior is restored (i.e., it runs without crashing and doesn't log).
    g_five_ins_hook_log.clear();
    target_five_instructions(20);
    ASSERT_TRUE(g_five_ins_hook_log.empty());
    std::cout << "  Original function behavior restored."
 << std::endl;

    std::cout << "--- HookFiveInstructionFunction Test Finished ---"
 << std::endl;
}


