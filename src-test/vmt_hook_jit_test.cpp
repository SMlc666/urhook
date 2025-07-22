#include <gtest/gtest.h>
#include <ur/vmt_hook.h>
#include <ur/jit.h>

namespace ur::vmt_hook_jit_test {

    // 1. Define the target class with a virtual method
    class TargetClass {
    public:
        virtual int calculate(int a, int b) {
            return a + b;
        }
    };

    // 2. Define our C++ handler function
    // This function will be called by our JIT-compiled detour.
    // It takes the original function's return value and modifies it.
    int detour_handler(int original_result) {
        return original_result * 10;
    }

}

TEST(VmtHookJitTest, CreateDetourWithJit) {
    using namespace ur::vmt_hook_jit_test;
    using namespace ur::assembler;

    TargetClass instance;
    TargetClass* instance_ptr = &instance; // Use pointer to avoid devirtualization

    // First, hook the method to get its original address
    ur::VmtHook vmt_hook(instance_ptr);
    auto hook_for_address = vmt_hook.hook_method(0, reinterpret_cast<void*>(&detour_handler));
    auto original_func_ptr = hook_for_address->get_original<void*>();
    hook_for_address.reset(); // Unhook immediately, we just wanted the address

    // Check that the original function works as expected
    ASSERT_EQ(instance_ptr->calculate(5, 3), 8);

    // 3. Dynamically generate the detour using JIT
    ur::jit::Jit jit;

    // Save Link Register (LR) and a temporary register (X19) to the stack
    jit.stp(Register::X19, Register::LR, Register::SP, -16, true);

    // Call the original virtual function. Arguments (X0, X1) are passed through.
    // The result will be in X0.
    jit.gen_load_address(Register::X19, reinterpret_cast<uintptr_t>(original_func_ptr));
    jit.blr(Register::X19);

    // Now, X0 holds the result of the original function.
    // It also becomes the first argument for our C++ handler.

    // Call our C++ handler.
    jit.gen_load_address(Register::X19, reinterpret_cast<uintptr_t>(&detour_handler));
    jit.blr(Register::X19);

    // Restore LR and X19 from the stack
    jit.ldp(Register::X19, Register::LR, Register::SP, 16, true);

    // Return to the original caller. The final result is in X0.
    jit.ret();

    // Finalize the JIT code to get an executable function pointer
    auto detour_func = jit.finalize<void*>();
    ASSERT_NE(detour_func, nullptr);

    // 4. Hook the VMT to point to our dynamically generated detour
    auto final_hook = vmt_hook.hook_method(0, detour_func);

    // 5. Verify the result
    // Expected: (5 + 3) * 10 = 80
    ASSERT_EQ(instance_ptr->calculate(5, 3), 80);

    // The hook will be automatically unhooked when final_hook goes out of scope.
    // The JIT memory will be automatically freed when jit goes out of scope.
}
