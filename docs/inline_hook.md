# `ur::inline_hook` - 行内 Hook

`ur::inline_hook` 提供了一种强大的机制，可以在运行时修改函数行为，将目标函数的执行流程重定向到用户提供的回调函数。这对于调试、监控或修改现有代码逻辑非常有用，尤其是在没有源代码的情况下。

## 核心特性

- **RAII 设计**: `Hook` 对象的生命周期管理着 Hook 的状态。当对象被创建时，Hook 被激活；当对象被销毁时，Hook 会被自动移除，原始函数行为被恢复。
- **共享 Hook**: 同一个目标函数可以被多个 `Hook` 实例挂钩，形成一个 Hook 链。当目标函数被调用时，这些 Hook 会以相反的顺序（后挂钩的先执行）依次触发。
- **线程安全**: 内部实现确保了在多线程环境下挂钩和卸载操作的原子性和安全性。
- **调用原始函数**: 在 Hook 回调中，可以安全地调用原始函数（或其他在调用链中的 Hook），允许开发者在不破坏原始逻辑的基础上扩展功能。
- **动态启用/禁用**: 可以在运行时动态地启用或禁用一个已安装的 Hook，而无需销毁和重建 `Hook` 对象。

## API 概览

### `ur::inline_hook::Hook`

这是实现行内 Hook 的主要类。

#### 构造函数

```cpp
Hook(uintptr_t target, Callback callback);
```

- `target`: 目标函数的绝对地址。
- `callback`: 一个回调函数指针。当目标函数被调用时，该回调函数将被执行。`Callback` 的类型是 `void*`，你需要将其转换为与目标函数签名匹配的函数指针。

#### `call_original<Ret, ...Args>(Args... args)`

在 Hook 回调函数内部，用于调用原始函数（或调用链中的下一个 Hook）。

- `Ret`: 原始函数的返回类型。
- `Args...`: 原始函数的参数类型。
- `args...`: 传递给原始函数的参数。

#### `is_valid()`

检查 Hook 是否已成功安装且当前有效。

#### `enable()`

启用一个之前被禁用的 Hook。

#### `disable()`

临时禁用一个 Hook，调用目标函数将直接执行原始实现，但 `Hook` 对象本身仍然存在，可以随时重新启用。

#### `unhook()`

永久移除 Hook。此操作不可逆。通常情况下，你应该依赖 `Hook` 对象的析构函数来自动完成此操作。

## 使用示例

### 1. 基本 Hook

下面的示例演示了如何 Hook 一个简单的函数，修改其返回值。

```cpp
#include <ur/inline_hook.h>
#include <iostream>

// 目标函数
int target_function(int a, int b) {
    return a + b;
}

// 全局 Hook 指针，用于在回调中调用原始函数
ur::inline_hook::Hook* g_hook = nullptr;

// Hook 回调函数
int hook_callback(int a, int b) {
    std::cout << "Hook a=" << a << ", b=" << b << std::endl;
    // 调用原始函数，并获取结果
    int original_result = g_hook->call_original<int>(a, b);
    // 修改结果
    return original_result + 10;
}

void basic_hook_example() {
    // 创建 Hook 对象，自动安装 Hook
    ur::inline_hook::Hook hook(
        reinterpret_cast<uintptr_t>(&target_function),
        reinterpret_cast<ur::inline_hook::Hook::Callback>(&hook_callback)
    );
    g_hook = &hook; // 将 hook 实例地址赋给全局指针

    if (hook.is_valid()) {
        std::cout << "Hook installed successfully." << std::endl;
        int result = target_function(5, 3); // 调用被 Hook 的函数
        std::cout << "Result: " << result << std::endl; // 预期输出: 18 ( (5+3) + 10 )
    }
    // 当 'hook' 对象离开作用域时，Hook 会被自动移除
}
```

### 2. 共享 Hook (Hook 链)

多个 Hook 可以作用于同一个函数，形成一个调用链。

```cpp
#include <ur/inline_hook.h>
#include <iostream>

// ... (target_function 定义同上)

ur::inline_hook::Hook* g_hook1 = nullptr;
ur::inline_hook::Hook* g_hook2 = nullptr;

// 第一个 Hook 回调
int hook_callback_1(int a, int b) {
    std::cout << "Hook 1 called." << std::endl;
    return g_hook1->call_original<int>(a, b) + 10;
}

// 第二个 Hook 回调
int hook_callback_2(int a, int b) {
    std::cout << "Hook 2 called." << std::endl;
    return g_hook2->call_original<int>(a, b) * 2;
}

void shared_hook_example() {
    ur::inline_hook::Hook hook1(
        reinterpret_cast<uintptr_t>(&target_function),
        reinterpret_cast<ur::inline_hook::Hook::Callback>(&hook_callback_1)
    );
    g_hook1 = &hook1;

    ur::inline_hook::Hook hook2(
        reinterpret_cast<uintptr_t>(&target_function),
        reinterpret_cast<ur::inline_hook::Hook::Callback>(&hook_callback_2)
    );
    g_hook2 = &hook2;

    // 调用顺序: hook_callback_2 -> hook_callback_1 -> target_function
    // 计算过程: ((5 + 3) + 10) * 2 = 36
    int result = target_function(5, 3);
    std::cout << "Shared hook result: " << result << std::endl;
}
```

### 3. Hook JIT 编译的函数

`inline_hook` 同样可以作用于由 `ur::jit` 动态生成的函数。

```cpp
#include <ur/inline_hook.h>
#include <ur/jit.h>
#include <iostream>

using JitFunc = int (*)(int, int);
ur::inline_hook::Hook* g_jit_hook = nullptr;

int jit_hook_callback(int a, int b) {
    std::cout << "JIT Hook called." << std::endl;
    return g_jit_hook->call_original<int>(a, b) + 100;
}

void jit_hook_example() {
    ur::jit::Jit jit_compiler;
    jit_compiler.add(ur::assembler::Register::X0, ur::assembler::Register::X0, ur::assembler::Register::X1);
    jit_compiler.ret();
    JitFunc jit_func = jit_compiler.finalize<JitFunc>();

    std::cout << "JIT function original result: " << jit_func(10, 5) << std::endl;

    ur::inline_hook::Hook hook(
        reinterpret_cast<uintptr_t>(jit_func),
        reinterpret_cast<ur::inline_hook::Hook::Callback>(&jit_hook_callback)
    );
    g_jit_hook = &hook;

    std::cout << "JIT function hooked result: " << jit_func(10, 5) << std::endl; // 预期: (10+5)+100 = 115
}
```
