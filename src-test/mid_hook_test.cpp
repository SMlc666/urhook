#include "gtest/gtest.h"
#include "ur/mid_hook.h"
#include "ur/jit.h" // Include JIT header
#include <iostream>
#include <vector> // For logging
#include <thread>
#include <atomic>
#include <chrono>

// A global flag to check if our callback was executed.
static bool g_callback_executed = false;
// A global variable to check if the context is passed correctly.
static uint64_t g_modified_register_value = 0;
// Global log for hook calls
static std::vector<std::string> g_mid_hook_jit_call_log;

// The function we are going to hook.
// It's volatile to prevent the compiler from optimizing it away.
volatile int target_function(int a, int b) {
    int result = a + b;
    // We will hook the instruction right after the addition.
    // The purpose of this log is to have a stable instruction to hook.
    std::cout << "Executing target_function: " << result << std::endl;
    return result;
}

// The callback function for our MidHook.
void hook_callback(ur::mid_hook::CpuContext* context) {
    g_callback_executed = true;
    // Let's modify X0 (which holds the first argument 'a' at the beginning)
    // to a specific value to verify context passing.
    g_modified_register_value = 0xDEADBEEF;
    context->gpr[0] = g_modified_register_value;
}

// Callback for JIT MidHook test
void mid_hook_jit_callback(ur::mid_hook::CpuContext* context) {
    g_mid_hook_jit_call_log.push_back("MidHook JIT callback executed");
    // Modify a register to see if it affects the return value (it should, as original code is skipped)
    context->gpr[0] = 0xCAFEBABE; // Modify X0 (return register for int)
}

TEST(MidHookTest, BasicHook) {
    // Reset flags before the test.
    g_callback_executed = false;
    g_modified_register_value = 0;

    // Find the address of the instruction to hook.
    // For this test, we'll just hook the beginning of the function.
    uintptr_t target_address = reinterpret_cast<uintptr_t>(&target_function);

    // Create the hook.
    ur::mid_hook::MidHook hook(target_address, &hook_callback);
    ASSERT_TRUE(hook.is_valid());

    // Call the hooked function.
    volatile int result = target_function(5, 10);

    // Verify that our callback was executed.
    EXPECT_TRUE(g_callback_executed);

    // Since our callback modifies X0 and the detour now returns immediately,
    // the final result of the function should be what we set in the callback.
    // We must cast the expected value to int to match the function's return type.
    EXPECT_EQ(result, static_cast<int>(g_modified_register_value));
}

// Test the move constructor
TEST(MidHookTest, MoveConstruction) {
    g_callback_executed = false;
    uintptr_t target_address = reinterpret_cast<uintptr_t>(&target_function);

    ur::mid_hook::MidHook hook1(target_address, &hook_callback);
    ASSERT_TRUE(hook1.is_valid());

    ur::mid_hook::MidHook hook2(std::move(hook1));
    ASSERT_TRUE(hook2.is_valid());
    ASSERT_FALSE(hook1.is_valid()); // NOLINT

    target_function(1, 2);
    EXPECT_TRUE(g_callback_executed);
}

// Test the move assignment operator
TEST(MidHookTest, MoveAssignment) {
    g_callback_executed = false;
    uintptr_t target_address = reinterpret_cast<uintptr_t>(&target_function);

    ur::mid_hook::MidHook hook1(target_address, &hook_callback);
    ASSERT_TRUE(hook1.is_valid());

    ur::mid_hook::MidHook hook2(target_address, &hook_callback);
    hook2 = std::move(hook1);

    ASSERT_TRUE(hook2.is_valid());
    ASSERT_FALSE(hook1.is_valid()); // NOLINT

    target_function(3, 4);
    EXPECT_TRUE(g_callback_executed);
}

// New test case for MidHooking a JIT-compiled function
TEST(MidHookTest, HookJitCompiledFunction) {
    std::cout << "--- Running HookJitCompiledFunction Test ---" << std::endl;
    g_mid_hook_jit_call_log.clear();

    // 1. JIT compile a simple function: int func(int a, int b) { return a + b; }
    ur::jit::Jit jit_compiler;
    jit_compiler.add(ur::assembler::Register::X0, ur::assembler::Register::X0, ur::assembler::Register::X1); // x0 = x0 + x1
    jit_compiler.ret();

    using JitTargetFunc = int (*)(int, int);
    JitTargetFunc jit_func = jit_compiler.finalize<JitTargetFunc>();
    ASSERT_NE(jit_func, nullptr) << "Failed to JIT compile function.";
    std::cout << "  JIT function compiled at: 0x" << std::hex << reinterpret_cast<uintptr_t>(jit_func) << std::dec << std::endl;

    // Verify the JIT compiled function works as expected before hooking
    EXPECT_EQ(jit_func(5, 7), 12);

    // 2. Hook the JIT compiled function
    {
        ur::mid_hook::MidHook hook(reinterpret_cast<uintptr_t>(jit_func), &mid_hook_jit_callback);
        ASSERT_TRUE(hook.is_valid());
        std::cout << "  MidHook installed on JIT function." << std::endl;

        // 3. Call the hooked JIT function and verify.
        // Since MidHook replaces the original code and returns from the callback,
        // the original JIT code (a + b) will NOT be executed.
        // The return value will be whatever X0 is set to in the callback.
        int result = jit_func(10, 20);
        EXPECT_EQ(result, static_cast<int>(0xCAFEBABE)); // Expected value from callback
        ASSERT_EQ(g_mid_hook_jit_call_log.size(), 1);
        EXPECT_EQ(g_mid_hook_jit_call_log[0], "MidHook JIT callback executed");
        std::cout << "  Hooked JIT function called successfully. Result: " << result << std::endl;
    } // 'hook' object goes out of scope here, triggering unhook()

    // 4. Verify original behavior after hook has been uninstalled.
    g_mid_hook_jit_call_log.clear(); // Clear log for next check
    EXPECT_EQ(jit_func(15, 25), 40); // Should be original behavior now (a + b)
    ASSERT_TRUE(g_mid_hook_jit_call_log.empty()); // No hook calls
    std::cout << "  Hook uninstalled (via destructor). Original JIT function behavior restored." << std::endl;

    // Release JIT memory
    jit_compiler.release();

    std::cout << "--- HookJitCompiledFunction Test Finished ---" << std::endl;
}

// --- Multi-threaded Stress Test ---

static std::atomic<bool> g_multithread_stop_flag;
static std::atomic<int> g_multithread_hook_cb_count;
static std::atomic<int> g_multithread_target_call_count;

volatile int multithread_target(int x) {
    g_multithread_target_call_count++;
    // Sleep for a very short duration to increase the chance of being suspended
    // while the hook is being applied or removed.
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    return x + 1;
}

void multithread_callback(ur::mid_hook::CpuContext* ctx) {
    g_multithread_hook_cb_count++;
    ctx->gpr[0] = 42; // Change the return value to a known constant
}

void worker_thread_main() {
    while (!g_multithread_stop_flag) {
        multithread_target(10);
    }
}

TEST(MidHookTest, EnableDisableUnhook) {
    g_callback_executed = false;
    uintptr_t target_address = reinterpret_cast<uintptr_t>(&target_function);

    ur::mid_hook::MidHook hook(target_address, &hook_callback);
    ASSERT_TRUE(hook.is_valid());

    // 1. Test disable
    ASSERT_TRUE(hook.disable());
    g_callback_executed = false;
    target_function(1, 2);
    ASSERT_FALSE(g_callback_executed);

    // 2. Test enable
    ASSERT_TRUE(hook.enable());
    g_callback_executed = false;
    target_function(1, 2);
    ASSERT_TRUE(g_callback_executed);

    // 3. Test unhook
    hook.unhook();
    ASSERT_TRUE(hook.is_valid()); // <-- 修改：对象仍然有效
    g_callback_executed = false;
    target_function(1, 2);
    ASSERT_FALSE(g_callback_executed);
}