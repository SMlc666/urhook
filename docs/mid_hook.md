# `ur::mid_hook` - 函数中间 Hook

`ur::mid_hook` 提供了一种在函数内部任意位置插入代码的机制，也称为“中间函数 Hook”。与 `inline_hook` 修改函数入口不同，`mid_hook` 在函数执行到特定指令时触发，允许开发者检查或修改当时的程序状态（如寄存器），然后继续执行原始函数的流程。

这对于需要在特定执行点进行调试、状态检查或逻辑注入的场景非常有用。

## 核心特性

- **精确位置 Hook**: 可以在函数的任何地址点设置 Hook。
- **上下文访问**: 回调函数接收一个 `CpuContext` 结构体指针，允许读取和修改所有通用寄存器（GPRs）的值。
- **自动上下文管理**: Hook 机制会自动保存和恢复执行上下文，确保原始函数逻辑的无缝继续。
- **RAII 设计**: 与 `inline_hook` 类似，`MidHook` 对象的生命周期管理着 Hook 的安装与卸载。
- **动态启用/禁用**: 支持在运行时动态启用或禁用已安装的 Hook。

## API 概览

### `ur::mid_hook::MidHook`

实现中间函数 Hook 的主要类。

#### 构造函数

```cpp
MidHook(uintptr_t target, Callback callback);
```

- `target`: Hook 的目标地址，即函数内部某条指令的地址。
- `callback`: Hook 触发时执行的回调函数。

#### `ur::mid_hook::CpuContext`

一个结构体，包含了所有通用寄存器（x0-x28, fp, lr）的值。

```cpp
struct CpuContext {
    uint64_t gpr[31]; // gpr[0] 是 x0, gpr[29] 是 fp, gpr[30] 是 lr
};
```

#### `ur::mid_hook::Callback`

回调函数的类型定义。

```cpp
using Callback = void (*)(CpuContext* context);
```

- `context`: 指向 `CpuContext` 的指针，允许在回调中检查和修改寄存器。

#### `is_valid()`, `enable()`, `disable()`, `unhook()`

这些方法的行为与 `ur::inline_hook::Hook` 中的同名方法一致。

## 使用示例

### 1. 在函数中间修改寄存器

这个例子演示了如何在一个函数执行过程中，通过 MidHook 修改寄存器的值，从而影响后续的计算。

```cpp
#include <ur/mid_hook.h>
#include <iostream>

// 一个简单的目标函数
__attribute__((__noinline__))
int target_function_for_mid_hook(int a, int b) {
    int c = a + b;
    // 我们将在这里插入 Hook
    int d = c * 2;
    return d;
}

// MidHook 回调函数
void mid_hook_callback(ur::mid_hook::CpuContext* context) {
    std::cout << "MidHook triggered!" << std::endl;
    // 根据 aarch64 调用约定, a 在 w0, b 在 w1
    // 假设在 Hook 点, c 的值存储在 w0 (context->gpr[0])
    // 我们将 w0 的值修改为 100
    context->gpr[0] = 100;
}

void mid_hook_example() {
    // **重要**: 确定 Hook 地址需要反汇编目标函数。
    // 这是一个示例，实际地址可能不同。
    // 假设我们通过反汇编得知 `int d = c * 2;` 这行代码的地址。
    uintptr_t hook_address = reinterpret_cast<uintptr_t>(&target_function_for_mid_hook) + 12; // 假设偏移为12

    ur::mid_hook::MidHook hook(hook_address, &mid_hook_callback);

    if (hook.is_valid()) {
        std::cout << "MidHook installed." << std::endl;
        // 原始逻辑: c = 5 + 3 = 8; d = 8 * 2 = 16.
        // Hook 后: c = 5 + 3 = 8; (Hook触发, c变为100); d = 100 * 2 = 200.
        int result = target_function_for_mid_hook(5, 3);
        std::cout << "Result after MidHook: " << result << std::endl; // 预期: 200
    }
}
```

### 2. Hook JIT 编译的函数

MidHook 也可以用于 Hook 动态生成的代码，例如实现一个“无限生命”的游戏作弊功能。

```cpp
#include <ur/mid_hook.h>
#include <ur/jit.h>
#include <iostream>

class Player {
public:
    int health = 100;
    void take_damage(int amount) {
        health -= amount;
    }
};

// 回调函数，用于阻止伤害
void god_mode_callback(ur::mid_hook::CpuContext* context) {
    // 在 aarch64 中，第二个参数 (amount) 在 w1 (gpr[1])
    // 我们将伤害值直接修改为 0
    context->gpr[1] = 0;
}

void mid_hook_jit_example() {
    Player player;
    ur::jit::Jit jit;
    // JIT 一个简单的函数: void func(Player* p, int amount) { p->take_damage(amount); }
    jit.gen_load_address(ur::assembler::Register::X16, reinterpret_cast<uintptr_t>(&Player::take_damage));
    jit.blr(ur::assembler::Register::X16);
    jit.ret();
    auto deal_damage_func = jit.finalize<void(*)(Player*, int)>();

    // 在 JIT 函数入口处 Hook
    ur::mid_hook::MidHook hook(reinterpret_cast<uintptr_t>(deal_damage_func), &god_mode_callback);

    std::cout << "Player health before: " << player.health << std::endl;
    deal_damage_func(&player, 50); // 尝试造成 50 点伤害
    std::cout << "Player health after: " << player.health << std::endl; // 预期: 100 (伤害被阻止)
}
```
