#include "ur/inline_hook.h"
#include <gtest/gtest.h>
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <chrono>

// --- Test Target Functions ---

// A simple function to be hooked.
int target_function_to_hook(int a, int b) {
std::cout << "  Original function called with: " << a << ", " << b << std::endl;
return a + b;
}

// A very short function.
__attribute__((noinline)) int short_target_function(int x) {
return x * 2;
}

// A function with no return value.
static int g_void_func_indicator = 0;
void target_void_return(int x) {
g_void_func_indicator = x;
std::cout << "  Original void function called with: " << x << std::endl;
}

// A function with many arguments to test register and stack passing.
__attribute__((noinline)) long long target_many_args(int a, int b, long c, long d, int e, int f, long long g, int h, int i, int j) {
std::cout << "  Original many_args function called." << std::endl;
return a + b + c + d + e + f + g + h + i + j;
}

// A function with floating point arguments.
double target_float_args(double a, float b, int c) {
std::cout << "  Original float_args function called." << std::endl;
return a + b + c;
}

// A function that throws an exception.
class TestException : public std::runtime_error {
public:
TestException() : std::runtime_error("Test Exception") {}
};

void target_throws_exception(int a) {
std::cout << "  Original function: about to throw for value " << a << std::endl;
if (a > 0) {
throw TestException();
}
}

// --- Global state for testing hooks ---
static std::vector<std::string> g_hook_call_log;
// These are raw, non-owning pointers. Their lifetime is managed by the stack objects in each test.
static ur::inline_hook::Hook* g_hook1 = nullptr;
static ur::inline_hook::Hook* g_hook2 = nullptr;
static ur::inline_hook::Hook* g_hook3 = nullptr;

// --- Hook Callback Functions ---

int hook_callback_1(int a, int b) {
g_hook_call_log.push_back("Hook 1 called");
std::cout << "  Hook 1: Before calling original. Args: " << a << ", " << b << std::endl;
int result = g_hook1->call_original<int>(a, b);
std::cout << "  Hook 1: After calling original. Result: " << result << std::endl;
return result + 10;
}

int hook_callback_2(int a, int b) {
g_hook_call_log.push_back("Hook 2 called");
std::cout << "  Hook 2: Before calling original. Args: " << a << ", " << b << std::endl;
int result = g_hook2->call_original<int>(a, b);
std::cout << "  Hook 2: After calling original. Result: " << result << std::endl;
return result * 2;
}

int short_hook_callback(int x) {
g_hook_call_log.push_back("Short hook called");
return 99; // Don't call original, just return a fixed value.
}

void void_hook_callback(int x) {
g_hook_call_log.push_back("Void hook called");
std::cout << "  Void hook: before calling original." << std::endl;
g_hook1->call_original<void>(x * 2);
std::cout << "  Void hook: after calling original." << std::endl;
}

long long many_args_hook_callback(int a, int b, long c, long d, int e, int f, long long g, int h, int i, int j) {
g_hook_call_log.push_back("Many args hook called");
long long result = g_hook1->call_original<long long>(a, b, c, d, e, f, g, h, i, j);
return result + 1;
}

double float_args_hook_callback(double a, float b, int c) {
g_hook_call_log.push_back("Float args hook called");
std::cout << "  Float args hook: before calling original." << std::endl;
double result = g_hook1->call_original<double>(a, b, c);
std::cout << "  Float args hook: after calling original." << std::endl;
return result + 1.0;
}

void exception_hook_callback(int a) {
g_hook_call_log.push_back("Exception hook called");
std::cout << "  Exception hook: before calling original." << std::endl;
try {
g_hook1->call_original<void>(a);
g_hook_call_log.push_back("Exception hook survived");
} catch (const TestException& e) {
g_hook_call_log.push_back("Exception hook caught exception");
throw;
}
}

// --- Test Fixture ---

class InlineHookTest : public ::testing::Test {
protected:
void SetUp() override {
g_hook_call_log.clear();
g_hook1 = nullptr;
g_hook2 = nullptr;
g_hook3 = nullptr;
g_void_func_indicator = 0;
}

void TearDown() override {  
    g_hook1 = nullptr;  
    g_hook2 = nullptr;  
    g_hook3 = nullptr;  
}

};

// --- Test Cases ---

TEST_F(InlineHookTest, SingleHook) {
std::cout << "--- Running SingleHook Test ---" << std::endl;
ur::inline_hook::Hook hook(
reinterpret_cast<uintptr_t>(&target_function_to_hook),
reinterpret_cast<ur::inline_hook::Hook::Callback>(&hook_callback_1)
);
g_hook1 = &hook;
ASSERT_TRUE(hook.is_valid());

int result = target_function_to_hook(5, 3);  
EXPECT_EQ(result, (5 + 3) + 10);  
ASSERT_EQ(g_hook_call_log.size(), 1);  
EXPECT_EQ(g_hook_call_log[0], "Hook 1 called");  
std::cout << "--- SingleHook Test Finished ---" << std::endl;

}

TEST_F(InlineHookTest, SharedHook) {
std::cout << "--- Running SharedHook Test ---" << std::endl;
ur::inline_hook::Hook hook1(
reinterpret_cast<uintptr_t>(&target_function_to_hook),
reinterpret_cast<ur::inline_hook::Hook::Callback>(&hook_callback_1)
);
g_hook1 = &hook1;
ASSERT_TRUE(hook1.is_valid());

ur::inline_hook::Hook hook2(  
    reinterpret_cast<uintptr_t>(&target_function_to_hook),  
    reinterpret_cast<ur::inline_hook::Hook::Callback>(&hook_callback_2)  
);  
g_hook2 = &hook2;  
ASSERT_TRUE(hook2.is_valid());  

int result = target_function_to_hook(10, 2);  
EXPECT_EQ(result, ((10 + 2) + 10) * 2);  
ASSERT_EQ(g_hook_call_log.size(), 2);  
EXPECT_EQ(g_hook_call_log[0], "Hook 2 called");  
EXPECT_EQ(g_hook_call_log[1], "Hook 1 called");  
std::cout << "--- SharedHook Test Finished ---" << std::endl;

}

TEST_F(InlineHookTest, RaiiUnhook) {
std::cout << "--- Running RaiiUnhook Test ---" << std::endl;
{
ur::inline_hook::Hook hook(
reinterpret_cast<uintptr_t>(&target_function_to_hook),
reinterpret_cast<ur::inline_hook::Hook::Callback>(&hook_callback_1)
);
g_hook1 = &hook;
ASSERT_TRUE(hook.is_valid());
int result = target_function_to_hook(7, 7);
EXPECT_EQ(result, (7 + 7) + 10);
}
int result = target_function_to_hook(7, 7);
EXPECT_EQ(result, 14);
std::cout << "--- RaiiUnhook Test Finished ---" << std::endl;
}

TEST_F(InlineHookTest, ShortFunctionHookNoOriginalCall) {
std::cout << "--- Running ShortFunctionHook Test ---" << std::endl;
ur::inline_hook::Hook hook(
reinterpret_cast<uintptr_t>(&short_target_function),
reinterpret_cast<ur::inline_hook::Hook::Callback>(&short_hook_callback)
);
ASSERT_TRUE(hook.is_valid());
int result = short_target_function(10);
EXPECT_EQ(result, 99);
ASSERT_EQ(g_hook_call_log.size(), 1);
EXPECT_EQ(g_hook_call_log[0], "Short hook called");
std::cout << "--- ShortFunctionHook Test Finished ---" << std::endl;
}

TEST_F(InlineHookTest, VoidReturnFunction) {
std::cout << "--- Running VoidReturn Test ---" << std::endl;
ur::inline_hook::Hook hook(
reinterpret_cast<uintptr_t>(&target_void_return),
reinterpret_cast<ur::inline_hook::Hook::Callback>(&void_hook_callback)
);
g_hook1 = &hook;
ASSERT_TRUE(hook.is_valid());

target_void_return(10);  
EXPECT_EQ(g_void_func_indicator, 20);  
ASSERT_EQ(g_hook_call_log.size(), 1);  
EXPECT_EQ(g_hook_call_log[0], "Void hook called");  
std::cout << "--- VoidReturn Test Finished ---" << std::endl;

}

TEST_F(InlineHookTest, ManyArgumentsFunction) {
std::cout << "--- Running ManyArguments Test ---" << std::endl;
ur::inline_hook::Hook hook(
reinterpret_cast<uintptr_t>(&target_many_args),
reinterpret_cast<ur::inline_hook::Hook::Callback>(&many_args_hook_callback)
);
g_hook1 = &hook;
ASSERT_TRUE(hook.is_valid());

long long result = target_many_args(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);  
long long expected = 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10;  
EXPECT_EQ(result, expected + 1);  
ASSERT_EQ(g_hook_call_log.size(), 1);  
EXPECT_EQ(g_hook_call_log[0], "Many args hook called");  
std::cout << "--- ManyArguments Test Finished ---" << std::endl;

}

TEST_F(InlineHookTest, FloatArgumentsFunction) {
std::cout << "--- Running FloatArguments Test ---" << std::endl;
ur::inline_hook::Hook hook(
reinterpret_cast<uintptr_t>(&target_float_args),
reinterpret_cast<ur::inline_hook::Hook::Callback>(&float_args_hook_callback)
);
g_hook1 = &hook;
ASSERT_TRUE(hook.is_valid());

double result = target_float_args(3.14, 2.71f, 10);  
double expected = 3.14 + 2.71f + 10.0;  
// Use a reasonable tolerance for float/double comparisons.  
ASSERT_NEAR(result, expected + 1.0, 1e-6f);  
ASSERT_EQ(g_hook_call_log.size(), 1);  
EXPECT_EQ(g_hook_call_log[0], "Float args hook called");  
std::cout << "--- FloatArguments Test Finished ---" << std::endl;

}

TEST_F(InlineHookTest, UnhookOrder) {
std::cout << "--- Running UnhookOrder Test ---" << std::endl;
int final_result = 0;
{
ur::inline_hook::Hook hook1(
reinterpret_cast<uintptr_t>(&target_function_to_hook),
reinterpret_cast<ur::inline_hook::Hook::Callback>(&hook_callback_1)
);
g_hook1 = &hook1;
ASSERT_TRUE(hook1.is_valid());

{  
        ur::inline_hook::Hook hook2(  
            reinterpret_cast<uintptr_t>(&target_function_to_hook),  
            reinterpret_cast<ur::inline_hook::Hook::Callback>(&hook_callback_2)  
        );  
        g_hook2 = &hook2;  
        ASSERT_TRUE(hook2.is_valid());  

        // hook2 -> hook1 -> original  
        int result = target_function_to_hook(5, 5);  
        EXPECT_EQ(result, ((5 + 5) + 10) * 2);  
    } // hook2 is destroyed here, hook chain should now be: hook1 -> original  

    g_hook_call_log.clear();  
    int result_after_h2_destroy = target_function_to_hook(5, 5);  
    EXPECT_EQ(result_after_h2_destroy, (5 + 5) + 10);  
    ASSERT_EQ(g_hook_call_log.size(), 1);  
    EXPECT_EQ(g_hook_call_log[0], "Hook 1 called");  

    final_result = result_after_h2_destroy;  

} // hook1 is destroyed here, hook chain should be empty  

EXPECT_EQ(final_result, 20);  
int original_result = target_function_to_hook(5, 5);  
EXPECT_EQ(original_result, 10);  

std::cout << "--- UnhookOrder Test Finished ---" << std::endl;

}

TEST_F(InlineHookTest, ExceptionPropagation) {
std::cout << "--- Running ExceptionPropagation Test ---" << std::endl;
ur::inline_hook::Hook hook(
reinterpret_cast<uintptr_t>(&target_throws_exception),
reinterpret_cast<ur::inline_hook::Hook::Callback>(&exception_hook_callback)
);
g_hook1 = &hook;
ASSERT_TRUE(hook.is_valid());

// Verify that the exception propagates all the way back to the caller.  
ASSERT_THROW(target_throws_exception(10), TestException);  

// Verify the hook was indeed called, and the exception was caught.  
ASSERT_EQ(g_hook_call_log.size(), 2);  
EXPECT_EQ(g_hook_call_log[0], "Exception hook called");  
EXPECT_EQ(g_hook_call_log[1], "Exception hook caught exception");  

g_hook_call_log.clear();  

// Also test the non-throwing case to be sure.  
ASSERT_NO_THROW(target_throws_exception(0));  
ASSERT_EQ(g_hook_call_log.size(), 2); // "Exception hook called", "Exception hook survived"  
EXPECT_EQ(g_hook_call_log[0], "Exception hook called");  
EXPECT_EQ(g_hook_call_log[1], "Exception hook survived");  

std::cout << "--- ExceptionPropagation Test Finished ---" << std::endl;

}

// --- Multi-threading Test ---

// Global state for multi-threading test
static std::atomic<int> g_atomic_counter(0);
static std::atomic<bool> g_stop_flag(false);

// Target function for multi-threading test
__attribute__((noinline)) void target_for_multithread() {
g_atomic_counter++;
}

// Hook callback for multi-threading test
void multithread_hook_callback() {
g_atomic_counter++;
g_hook1->call_original<void>();
}

TEST_F(InlineHookTest, MultiThreadedHooking) {
std::cout << "--- Running MultiThreadedHooking Test ---" << std::endl;
g_atomic_counter = 0;
g_stop_flag = false;

ur::inline_hook::Hook hook(  
    reinterpret_cast<uintptr_t>(&target_for_multithread),  
    reinterpret_cast<ur::inline_hook::Hook::Callback>(&multithread_hook_callback)  
);  
g_hook1 = &hook;  
ASSERT_TRUE(hook.is_valid());  

const int num_threads = 4;  
std::vector<std::thread> threads;  
std::atomic<int> call_count(0);  

for (int i = 0; i < num_threads; ++i) {  
    threads.emplace_back([&]() {  
        while (!g_stop_flag) {  
            target_for_multithread();  
            call_count++;  
        }  
    });  
}  

std::this_thread::sleep_for(std::chrono::seconds(1));  
g_stop_flag = true;  

for (auto& t : threads) {  
    t.join();  
}  

// Each call to target_for_multithread should increment the counter twice (once in hook, once in original)  
EXPECT_EQ(g_atomic_counter.load(), call_count.load() * 2);  
std::cout << "--- MultiThreadedHooking Test Finished ---" << std::endl;

}

TEST_F(InlineHookTest, EnableDisableUnhook) {
    ur::inline_hook::Hook hook(
        reinterpret_cast<uintptr_t>(&target_function_to_hook),
        reinterpret_cast<ur::inline_hook::Hook::Callback>(&hook_callback_1)
    );
    g_hook1 = &hook;
    ASSERT_TRUE(hook.is_valid());

    // 1. Test disable
    ASSERT_TRUE(hook.disable());
    g_hook_call_log.clear();
    int result = target_function_to_hook(1, 2);
    ASSERT_TRUE(g_hook_call_log.empty());
    ASSERT_EQ(result, 3);

    // 2. Test enable
    ASSERT_TRUE(hook.enable());
    g_hook_call_log.clear();
    result = target_function_to_hook(1, 2);
    ASSERT_EQ(g_hook_call_log.size(), 1);
    ASSERT_EQ(result, 13);

    // 3. Test unhook
    hook.unhook();
    ASSERT_FALSE(hook.is_valid());
    g_hook_call_log.clear();
    result = target_function_to_hook(1, 2);
    ASSERT_TRUE(g_hook_call_log.empty());
    ASSERT_EQ(result, 3);
}
