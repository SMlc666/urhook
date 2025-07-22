#include <gtest/gtest.h>
#include <ur/vmt_hook.h>
#include <ur/memory.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

namespace ur::vmt_hook_test {
    class TestClass {
    public:
        virtual int test_method(int val) {
            return val * 2;
        }

        virtual int another_method() {
            return 42;
        }
    };

    // A global variable to hold the hook object so the test hook can access it.
    std::unique_ptr<ur::VmHook> hook_handle;

    int hooked_test_method(TestClass* self, int val) {
        auto original_func = hook_handle->get_original<int (*)(TestClass*, int)>();
        return 100 + original_func(self, val);
    }
}

TEST(VmtHookTest, HookAndUnhook) {
    using namespace ur::vmt_hook_test;

    TestClass instance;
    TestClass* instance_ptr = &instance; // Use a pointer to prevent devirtualization
    ur::VmtHook vmt_hook(instance_ptr);

    // 1. Test original method behavior
    ASSERT_EQ(instance_ptr->test_method(10), 20);
    ASSERT_EQ(instance_ptr->another_method(), 42);

    // 2. Hook the method
    hook_handle = vmt_hook.hook_method(0, reinterpret_cast<void*>(&hooked_test_method));
    ASSERT_NE(hook_handle, nullptr);

    // 3. Test hooked method behavior
    // The hooked function should add 100 to the original result (10 * 2)
    ASSERT_EQ(instance_ptr->test_method(10), 120);
    
    // Ensure other methods are unaffected
    ASSERT_EQ(instance_ptr->another_method(), 42);

    // 4. Unhook the method by resetting the unique_ptr
    hook_handle.reset();

    // 5. Test if the method is restored to its original state
    ASSERT_EQ(instance_ptr->test_method(10), 20);
}

// --- Multi-threading Test for VmtHook ---

namespace ur::vmt_hook_multithread_test {

    static std::atomic<int> g_original_call_count(0);
    static std::atomic<int> g_hook_call_count(0);
    static std::atomic<bool> g_stop_flag(false);

    class MultiThreadTestClass {
    public:
        virtual void concurrent_method() {
            g_original_call_count++;
        }
        virtual ~MultiThreadTestClass() = default;
    };

    std::unique_ptr<ur::VmHook> mt_hook_handle;

    void hooked_concurrent_method(MultiThreadTestClass* self) {
        g_hook_call_count++;
        auto original_func = mt_hook_handle->get_original<void (*)(MultiThreadTestClass*)>();
        original_func(self);
    }
}

TEST(VmtHookTest, MultiThreadedHooking) {
    using namespace ur::vmt_hook_multithread_test;
    std::cout << "--- Running MultiThreadedVmtHook Test ---" << std::endl;

    g_original_call_count = 0;
    g_hook_call_count = 0;
    g_stop_flag = false;

    MultiThreadTestClass instance;
    MultiThreadTestClass* instance_ptr = &instance;
    ur::VmtHook vmt_hook(instance_ptr);

    // Hook the method
    mt_hook_handle = vmt_hook.hook_method(0, reinterpret_cast<void*>(&hooked_concurrent_method));
    ASSERT_NE(mt_hook_handle, nullptr);

    const int num_threads = 4;
    std::vector<std::thread> threads;
    std::atomic<int> total_calls(0);

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            while (!g_stop_flag) {
                instance_ptr->concurrent_method();
                total_calls++;
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    g_stop_flag = true;

    for (auto& t : threads) {
        t.join();
    }

    // Verify that both the hook and the original function were called the correct number of times.
    EXPECT_EQ(g_hook_call_count.load(), total_calls.load());
    EXPECT_EQ(g_original_call_count.load(), total_calls.load());

    // Unhook
    mt_hook_handle.reset();

    // Reset counters and call again to ensure it's unhooked
    g_original_call_count = 0;
    g_hook_call_count = 0;
    instance_ptr->concurrent_method();
    EXPECT_EQ(g_hook_call_count.load(), 0);
    EXPECT_EQ(g_original_call_count.load(), 1);

    std::cout << "--- MultiThreadedVmtHook Test Finished ---" << std::endl;
}

TEST(VmtHookTest, EnableDisableUnhook) {
    using namespace ur::vmt_hook_test;

    TestClass instance;
    TestClass* instance_ptr = &instance;
    ur::VmtHook vmt_hook(instance_ptr);

    hook_handle = vmt_hook.hook_method(0, reinterpret_cast<void*>(&hooked_test_method));
    ASSERT_NE(hook_handle, nullptr);

    // 1. Test disable
    ASSERT_TRUE(hook_handle->disable());
    ASSERT_EQ(instance_ptr->test_method(10), 20);

    // 2. Test enable
    ASSERT_TRUE(hook_handle->enable());
    ASSERT_EQ(instance_ptr->test_method(10), 120);

    // 3. Test unhook
    hook_handle->unhook();
    ASSERT_EQ(instance_ptr->test_method(10), 20);
}