#include <gtest/gtest.h>
#include <dlfcn.h>
#include <vector>
#include <string>
#include <iostream>

#include "ur/plthook.h"

// 记录替换调用日志
static std::vector<std::string> g_log;
static void* g_original_puts = nullptr;

// 替换函数，签名与 puts 一致
extern "C" int my_puts(const char* s) {
    g_log.push_back(std::string("hooked: ") + (s ? s : "(null)"));
    // 调用原始 puts
    auto orig = reinterpret_cast<int (*)(const char*)>(g_original_puts);
    if (orig) {
        return orig(s);
    }
    return 0;
}

// 一个本地函数用于 dladdr 获取主可执行文件基址
static int local_marker() { return 123; }

TEST(PltHookTest, HookAndUnhookPutsOnMainExecutable) {
    // 1. 获取主可执行文件基址
    Dl_info info{};
    ASSERT_NE(dladdr(reinterpret_cast<const void*>(&local_marker), &info), 0);
    auto base = reinterpret_cast<uintptr_t>(info.dli_fbase);
    ASSERT_NE(base, 0u);

    // 2. 创建 PLT Hook 对象（基于主程序）
    ur::plthook::Hook hook(base);
    ASSERT_TRUE(hook.is_valid());

    // 3. 安装 puts 钩子
    g_log.clear();
    g_original_puts = nullptr;
    bool ok = hook.hook_symbol("puts", reinterpret_cast<void*>(&my_puts), &g_original_puts);
    ASSERT_TRUE(ok);
    ASSERT_NE(g_original_puts, nullptr);

    // 4. 调用 puts，验证替换生效（日志应记录）
    const char* msg = "Hello from PLT hook";
    int ret = puts(msg);
    (void)ret;
    ASSERT_FALSE(g_log.empty());
    ASSERT_EQ(g_log.back(), std::string("hooked: ") + msg);

    // 5. 卸载钩子，恢复 GOT 原始值
    ASSERT_TRUE(hook.unhook_symbol("puts"));

    // 6. 清理日志，再次调用 puts，日志不应新增（说明已恢复）
    g_log.clear();
    ret = puts("After unhook");
    (void)ret;
    ASSERT_TRUE(g_log.empty());
}