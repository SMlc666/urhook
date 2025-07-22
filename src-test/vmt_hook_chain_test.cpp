#include <gtest/gtest.h>
#include <ur/vmt_hook.h>
#include <vector>
#include <string>

namespace ur::vmt_hook_chain_test {

    class Calculator {
    public:
        virtual int process(int value) {
            // In a real scenario, this might be an empty base implementation
            return value;
        }
    };

    // Use a global vector to trace the call order
    std::vector<std::string> call_trace;

    // Hook handles need to be accessible by the hook functions
    std::unique_ptr<ur::VmHook> hook_a_handle;
    std::unique_ptr<ur::VmHook> hook_b_handle;

    using ProcessFunc = int (*)(Calculator*, int);

    int hook_a(Calculator* self, int value) {
        call_trace.push_back("Hook A entered");
        auto original_func = hook_a_handle->get_original<ProcessFunc>();
        int result = original_func(self, value);
        call_trace.push_back("Hook A exiting");
        return result + 10; // Hook A adds 10
    }

    int hook_b(Calculator* self, int value) {
        call_trace.push_back("Hook B entered");
        auto original_func = hook_b_handle->get_original<ProcessFunc>();
        int result = original_func(self, value);
        call_trace.push_back("Hook B exiting");
        return result * 2; // Hook B doubles the result
    }

}

TEST(VmtHookChainTest, ChainedHooks) {
    using namespace ur::vmt_hook_chain_test;

    Calculator instance;
    Calculator* instance_ptr = &instance;
    ur::VmtHook vmt_hook(instance_ptr);

    // 1. Initial state: direct call to original
    ASSERT_EQ(instance_ptr->process(5), 5);

    // 2. Apply first hook (Hook A)
    hook_a_handle = vmt_hook.hook_method(0, reinterpret_cast<void*>(&hook_a));
    ASSERT_EQ(instance_ptr->process(5), 15); // 5 + 10

    // 3. Apply second hook (Hook B) on top of Hook A
    hook_b_handle = vmt_hook.hook_method(0, reinterpret_cast<void*>(&hook_b));
    
    // 4. Verify the full chain
    call_trace.clear();
    // Expected calculation: (5 + 10) * 2 = 30
    ASSERT_EQ(instance_ptr->process(5), 30);

    // Verify the call order
    std::vector<std::string> expected_trace = {
        "Hook B entered",
        "Hook A entered",
        "Hook A exiting",
        "Hook B exiting"
    };
    ASSERT_EQ(call_trace, expected_trace);

    // 5. Unhook B (the outer hook) by resetting its handle
    hook_b_handle.reset();
    call_trace.clear();

    // Verify that only Hook A remains active
    ASSERT_EQ(instance_ptr->process(5), 15); // 5 + 10
    expected_trace = {
        "Hook A entered",
        "Hook A exiting"
    };
    ASSERT_EQ(call_trace, expected_trace);

    // 6. Unhook A (the last hook)
    hook_a_handle.reset();
    
    // Verify that the VMT is restored to its original state
    ASSERT_EQ(instance_ptr->process(5), 5);
}
