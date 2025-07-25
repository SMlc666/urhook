#include <gtest/gtest.h>
#include <ur/vmt_hook.h>
#include <ur/memory.h>
#include <ur/maps_parser.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <dlfcn.h>
#include <typeinfo>
#include <unistd.h>

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

TEST(VmtHookTest, DirectVmtAddressHook) {
    using namespace ur::vmt_hook_test;

    TestClass instance;
    TestClass* instance_ptr = &instance;
    
    // Manually get the VMT address
    void** vmt_address = *reinterpret_cast<void***>(instance_ptr);

    ur::VmtHook vmt_hook(vmt_address);

    // 1. Test original method behavior
    ASSERT_EQ(instance_ptr->test_method(10), 20);

    // 2. Hook the method
    hook_handle = vmt_hook.hook_method(0, reinterpret_cast<void*>(&hooked_test_method));
    ASSERT_NE(hook_handle, nullptr);

    // 3. Test hooked method behavior
    ASSERT_EQ(instance_ptr->test_method(10), 120);

    // 4. Unhook the method
    hook_handle.reset();

    // 5. Test if the method is restored
    ASSERT_EQ(instance_ptr->test_method(10), 20);
}

TEST(VmtHookTest, FindVmtBySymbol) {
    using namespace ur::vmt_hook_test;

    // 1. Create an instance to compare against
    TestClass instance;
    TestClass* instance_ptr = &instance;
    void** vmt_from_instance = *reinterpret_cast<void***>(instance_ptr);

    // 2. Get the mangled name
    const char* mangled_name = typeid(TestClass).name();

    // 3. Construct the VMT symbol
    std::string vmt_symbol = "_ZTV" + std::string(mangled_name);

    // 4. Find the symbol using dlsym
    void* vmt_symbol_address = dlsym(RTLD_DEFAULT, vmt_symbol.c_str());
    ASSERT_NE(vmt_symbol_address, nullptr);

    // 5. Adjust the address to get the actual VMT address
    void** vmt_from_symbol = reinterpret_cast<void**>(
        static_cast<char*>(vmt_symbol_address) + 2 * sizeof(void*)
    );

    // 6. Verify that the VMT addresses from both methods are the same
    ASSERT_EQ(vmt_from_instance, vmt_from_symbol);
    std::cout << "VMT address from instance: " << vmt_from_instance << std::endl;
    std::cout << "VMT address from symbol (dlsym): " << vmt_from_symbol << std::endl;

    // 7. Test hooking using the VMT address found via symbol
    ur::VmtHook vmt_hook(vmt_from_symbol);
    ASSERT_EQ(instance_ptr->test_method(10), 20);

    auto hook = vmt_hook.hook_method(0, reinterpret_cast<void*>(&hooked_test_method));
    ASSERT_NE(hook, nullptr);
    hook_handle = std::move(hook);

    ASSERT_EQ(instance_ptr->test_method(10), 120);

    hook_handle.reset();
    ASSERT_EQ(instance_ptr->test_method(10), 20);
}

TEST(VmtHookTest, FindVmtBySymbolWithElfParser) {
    using namespace ur::vmt_hook_test;

    // 1. Get the path of the current executable
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    ASSERT_NE(len, -1);
    exe_path[len] = '\0';
    std::cout << "Executable path from readlink: " << exe_path << std::endl;

    // 2. Parse maps and find the map for the current executable
    auto maps = ur::maps_parser::MapsParser::parse();
    std::cout << "--- Maps from /proc/self/maps ---" << std::endl;
    for (const auto& map : maps) {
        std::cout << map.get_path() << std::endl;
    }
    std::cout << "---------------------------------" << std::endl;
    const auto* map_info = ur::maps_parser::MapsParser::find_map_by_path(maps, exe_path);
    ASSERT_NE(map_info, nullptr);

    // 3. Get the ElfParser for this map
    auto* elf_parser = map_info->get_elf_parser();
    ASSERT_NE(elf_parser, nullptr);

    // 4. Get the mangled name and construct the VMT symbol
    const char* mangled_name = typeid(TestClass).name();
    std::string vmt_symbol = "_ZTV" + std::string(mangled_name);

    // 5. Find the symbol's address using ElfParser
    uintptr_t symbol_address = elf_parser->find_symbol(vmt_symbol);
    ASSERT_NE(symbol_address, 0);

    // 6. Adjust the address to get the actual VMT address (skip offset_to_top and RTTI pointer)
    void** vmt_from_elf = reinterpret_cast<void**>(symbol_address + 2 * sizeof(void*));

    // 7. Get VMT from an instance for verification
    TestClass instance;
    TestClass* instance_ptr = &instance;
    void** vmt_from_instance = *reinterpret_cast<void***>(instance_ptr);

    // 8. Verify that the VMT addresses are the same
    ASSERT_EQ(vmt_from_instance, vmt_from_elf);
    std::cout << "VMT address from instance: " << vmt_from_instance << std::endl;
    std::cout << "VMT address from symbol (ElfParser): " << vmt_from_elf << std::endl;

    // 9. Test hooking using the VMT address found via ElfParser
    ur::VmtHook vmt_hook(vmt_from_elf);
    ASSERT_EQ(instance_ptr->test_method(10), 20);
    
    auto hook = vmt_hook.hook_method(0, reinterpret_cast<void*>(&hooked_test_method));
    ASSERT_NE(hook, nullptr);
    hook_handle = std::move(hook);

    ASSERT_EQ(instance_ptr->test_method(10), 120);

    hook_handle.reset();
    ASSERT_EQ(instance_ptr->test_method(10), 20);
}