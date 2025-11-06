# `ur::plthook` - 基于 PLT/GOT 的符号 Hook（ARM64）

`ur::plthook` 提供对已加载 ELF 模块的 PLT/GOT 条目进行重写，从而将通过 PLT 间接调用的外部符号重定向到自定义实现。该机制对 Android/Linux arm64-v8a 的共享库与主可执行文件生效。

- C++ 接口头文件: [include/ur/plthook.h](include/ur/plthook.h)
- 主要类入口: [C++ ur::plthook::Hook](include/ur/plthook.h:15)
- 实现参考: [src/plthook.cpp](src/plthook.cpp)
- C API 声明: [include/ur/capi.h](include/ur/capi.h)

依赖模块（已内置）
- ELF 与 PLT/REL 解析: [include/ur/elf_parser.h](include/ur/elf_parser.h)
- 内存保护与写入: [include/ur/memory.h](include/ur/memory.h)
- 进程内存映射: [include/ur/maps_parser.h](include/ur/maps_parser.h)

工作原理概述
- 解析目标 ELF 的动态段，获取:
  - DT_JMPREL（PLT 重定位表）地址与大小
  - DT_PLTREL（条目类型：REL 或 RELA）
- 遍历 .rel[a].plt 中的重定位项，筛选 AArch64 的 R_AARCH64_JUMP_SLOT（值 1026）
- 使用 dynsym/dynstr 解析重定位关联的符号名
- 将该条目的 r_offset（GOT 条目地址）暂时设置为可写，写入自定义函数指针，写回后恢复原权限
- 保存原始 GOT 指针，用于恢复与获取“原始函数”调用

适用与限制
- 适用：通过 PLT/GOT 间接调用的外部函数（如 libc 中的 puts/fopen 等）
- 不适用：直接调用（non-PLT）或内部静态符号；此类场景使用 inline/mid hook 更合适
- 支持 BIND_NOW 与懒加载（lazy binding）
- FULL RELRO 场景下 .got.plt 为只读，通过临时 mprotect 写入，再恢复 R
- 当前实现面向 AArch64；跨架构扩展可后续追加

快速开始（C++）
- 需包含: [include/ur/plthook.h](include/ur/plthook.h)
- 核心类型: [C++ ur::plthook::Hook](include/ur/plthook.h:15)

示例：Hook 主可执行文件中的 puts（通过 PLT 调用的 libc 符号）
```cpp
#include <ur/plthook.h>
#include <dlfcn.h>
#include <iostream>
#include <vector>
#include <string>

static void* g_original_puts = nullptr;

extern "C" int my_puts(const char* s) {
    std::cout << "[hooked] " << (s ? s : "(null)") << std::endl;
    auto orig = reinterpret_cast<int (*)(const char*)>(g_original_puts);
    return orig ? orig(s) : 0;
}

static int local_marker() { return 0; }

void example_plthook_main() {
    // 1) 定位主可执行文件基址
    Dl_info info{};
    if (!dladdr(reinterpret_cast<const void*>(&local_marker), &info)) return;
    auto base = reinterpret_cast<uintptr_t>(info.dli_fbase);

    // 2) 创建 Hook 会话（RAII）
    ur::plthook::Hook hook(base);
    if (!hook.is_valid()) {
        std::cerr << "ELF parse failed\n";
        return;
    }

    // 3) 安装 puts 钩子
    if (!hook.hook_symbol("puts", reinterpret_cast<void*>(&my_puts), &g_original_puts)) {
        std::cerr << "hook puts failed\n";
        return;
    }

    // 4) 调用 puts 观察重定向
    puts("Hello from PLT hook"); // 将进入 my_puts

    // 5) 恢复
    hook.unhook_symbol("puts");
}
```

快速开始（C API）
- 需包含: [include/ur/capi.h](include/ur/capi.h)
- C 层封装位于: src/plthook_capi.cpp（内部桥接到 C++ 实现）

示例：通过 so 路径创建并 Hook
```c
#include <ur/capi.h>
#include <stdio.h>

static void* g_orig_puts = 0;

int my_puts_c(const char* s) {
    typedef int (*puts_t)(const char*);
    puts_t orig = (puts_t)g_orig_puts;
    if (orig) return orig(s);
    return 0;
}

void example_plthook_c() {
    ur_plthook_t* h = 0;
    // 根据路径子串匹配定位目标库基址（例如 "libc.so"）
    if (ur_plthook_create_from_path("libc.so", &h) != UR_STATUS_OK) return;
    if (!ur_plthook_is_valid(h)) { ur_plthook_destroy(h); return; }

    if (ur_plthook_hook_symbol(h, "puts", (void*)&my_puts_c, &g_orig_puts) == UR_STATUS_OK) {
        puts("C API hooked puts");
        ur_plthook_unhook_symbol(h, "puts");
    }
    ur_plthook_destroy(h);
}
```

线程安全
- 对同一 `Hook` 实例的安装/卸载操作使用内部互斥进行串行化
- 不建议不同 `Hook` 实例同时操作同一 GOT 条目

错误处理与返回语义
- 解析失败（ELF 无效 / 无 DT_JMPREL / 无 dynsym/dynstr）：安装失败
- 权限修改或写入失败（如内核策略限制）：安装失败
- 符号未命中：安装失败
- 重复安装同一符号：将覆盖已安装的替换函数，返回原始函数指针不变

开发与测试
- 单元测试： [src-test/plthook_test.cpp](src-test/plthook_test.cpp)
- 构建脚本已包含通配 [xmake.lua](xmake.lua) 的 `src/**.cpp`，无需额外配置

注意事项
- Android 环境中，不同系统映像/安全策略可能影响 mprotect 行为；如失败，应回退到 inline/mid hook 方案
- 若目标二进制使用非标准链接器脚本或启用强化保护，PLT/GOT 钩子可用性可能受限
- 对高频调用点进行 GOT 重写具备较低运行时开销，但仍建议在关键路径上评估性能影响

相关文档
- 内存与 ELF 解析：[docs/elf_maps_parser.md](docs/elf_maps_parser.md)
- 内存操作：[docs/memory.md](docs/memory.md)