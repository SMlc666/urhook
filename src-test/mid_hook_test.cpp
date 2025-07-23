#include "gtest/gtest.h"
#include "ur/mid_hook.h"
#include "ur/jit.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

// Global flag to check if our callback was executed.
static bool g_callback_executed = false;
// Global variable to check if the context is passed correctly and modified.
static uint64_t g_modified_register_value = 0;

// The function we are going to hook.
// It's volatile to prevent the compiler from optimizing it away.
__attribute__((__noinline__))
volatile int target_function(int a, int b) {
    // Add some nops to make sure the function is large enough for any hook type.
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop");
    return a + b;
}

// A second target function for move tests.
__attribute__((__noinline__))
volatile int target_function_2(int a, int b) {
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop");
    return a * b;
}

// The callback function for our MidHook.
void hook_callback(ur::mid_hook::CpuContext* context) {
    g_callback_executed = true;
    // Modify a register to test context passing.
    // We'll modify a callee-saved register to see if it persists
    // after the original function returns.
    g_modified_register_value = 0xDEADBEEF;
    context->gpr[19] = g_modified_register_value; // Modify X19
}

// A simple callback that does nothing, for move tests.
void dummy_callback(ur::mid_hook::CpuContext* context) {
    // This callback does nothing.
}


TEST(MidHookTest, BasicHook) {
    g_callback_executed = false;
    g_modified_register_value = 0;
    uintptr_t target_address = reinterpret_cast<uintptr_t>(&target_function);

    ur::mid_hook::MidHook hook(target_address, &hook_callback);
    ASSERT_TRUE(hook.is_valid());

    // We need to check a callee-saved register before the call.
    uint64_t x19_before;
    asm volatile("mov %0, x19" : "=r"(x19_before));

    volatile int result = target_function(5, 10);

    // After the call, check if X19 was modified by our callback.
    uint64_t x19_after;
    asm volatile("mov %0, x19" : "=r"(x19_after));

    // Verify that the callback was executed.
    EXPECT_TRUE(g_callback_executed);
    // Verify that the original function's logic was executed.
    EXPECT_EQ(result, 15);
    // Verify that our modification to the context was successful.
    EXPECT_EQ(x19_after, g_modified_register_value);

    // Restore X19 to its original value to not affect other tests.
    asm volatile("mov x19, %0" : : "r"(x19_before));
}

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

TEST(MidHookTest, MoveAssignment) {
    g_callback_executed = false;
    uintptr_t target_address1 = reinterpret_cast<uintptr_t>(&target_function);
    uintptr_t target_address2 = reinterpret_cast<uintptr_t>(&target_function_2);

    ur::mid_hook::MidHook hook1(target_address1, &hook_callback);
    ASSERT_TRUE(hook1.is_valid());

    ur::mid_hook::MidHook hook2(target_address2, &dummy_callback);
    ASSERT_TRUE(hook2.is_valid());

    hook2 = std::move(hook1);

    ASSERT_TRUE(hook2.is_valid());
    ASSERT_FALSE(hook1.is_valid()); // NOLINT

    // hook2 should now be hooking target_function_1 with hook_callback.
    target_function(3, 4);
    EXPECT_TRUE(g_callback_executed);

    // target_function_2 should have been unhooked.
    g_callback_executed = false; // Reset flag
    target_function_2(5, 6);
    EXPECT_FALSE(g_callback_executed);
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
    ASSERT_FALSE(hook.is_valid()); // After unhook, it should be invalid.
    g_callback_executed = false;
    target_function(1, 2);
    ASSERT_FALSE(g_callback_executed);
}

// --- Start of new test case for JIT function hook ---

// Simulate a Player class in a game
class Player {
public:
    // The function signature for our JIT-compiled AddHealth
    using AddHealthFn = void (*)(Player*, int);

    int health_ = 100;
    AddHealthFn add_health_fn_ = nullptr;

private:
    std::optional<ur::jit::Jit> jit_compiler_;

public:
    Player() {
        jit_compiler_ = std::make_optional<ur::jit::Jit>();
        // The JIT-compiled function will follow the AAPCS64 calling convention.
        // X0 will hold 'this' pointer.
        // X1 will hold the 'amount' argument.
        // The 'health_' member is at offset 0 of the Player object.

        // Add X1 (amount) to the value at [X0] (this->health_)
        // 1. Load current health into a temporary register (W2)
        jit_compiler_->ldr(ur::assembler::Register::W2, ur::assembler::Register::X0, 0); // ldr w2, [x0]
        // 2. Add the amount
        jit_compiler_->add(ur::assembler::Register::W2, ur::assembler::Register::W2, ur::assembler::Register::W1); // add w2, w2, w1
        // 3. Store the new health back
        jit_compiler_->str(ur::assembler::Register::W2, ur::assembler::Register::X0, 0); // str w2, [x0]
        // 4. Return
        jit_compiler_->ret();

        add_health_fn_ = jit_compiler_->finalize<AddHealthFn>();
        if (!add_health_fn_) {
            throw std::runtime_error("Failed to JIT-compile AddHealth function.");
        }
    }

    void AddHealth(int amount) {
        if (add_health_fn_) {
            add_health_fn_(this, amount);
        }
    }
};

// The callback for MidHook to implement infinite health.
void infinite_health_callback(ur::mid_hook::CpuContext* context) {
    // The 'amount' is in the second argument register, X1, but passed as a 32-bit int.
    // We must treat the lower 32 bits of the register as a signed integer.
    auto amount = static_cast<int32_t>(context->gpr[1]);

    // If the amount is negative (damage), change it to 0.
    if (amount < 0) {
        context->gpr[1] = 0;
    }
    // If the amount is positive (healing), we let it pass through.
}

TEST(MidHookTest, JitFunctionInfiniteHealth) {
    // 1. Create our game object with a JIT-compiled method.
    Player player;
    ASSERT_NE(player.add_health_fn_, nullptr);
    ASSERT_EQ(player.health_, 100);

    // 2. Hook the JIT-compiled AddHealth method.
    uintptr_t target_address = reinterpret_cast<uintptr_t>(player.add_health_fn_);
    ur::mid_hook::MidHook hook(target_address, &infinite_health_callback);
    ASSERT_TRUE(hook.is_valid());

    // 3. Test the hook logic.

    // First, try to heal the player. This should work normally.
    player.AddHealth(20);
    EXPECT_EQ(player.health_, 120);

    // Now, try to damage the player. The hook should prevent this.
    player.AddHealth(-50);
    // The health should remain 120 because the callback changed -50 to 0.
    EXPECT_EQ(player.health_, 120);

    // Another damage test.
    player.AddHealth(-10);
    EXPECT_EQ(player.health_, 120);

    // Another healing test.
    player.AddHealth(30);
    EXPECT_EQ(player.health_, 150);

    // 4. Disable the hook and check if damage works again.
    ASSERT_TRUE(hook.disable());
    player.AddHealth(-40);
    EXPECT_EQ(player.health_, 110); // 150 - 40 = 110

    // 5. Re-enable and check if infinite health is back.
    ASSERT_TRUE(hook.enable());
    player.AddHealth(-100);
    EXPECT_EQ(player.health_, 110); // Should not change.
}

// --- End of new test case for JIT function hook ---