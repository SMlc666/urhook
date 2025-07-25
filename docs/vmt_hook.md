# `ur::vmt_hook` - 虚函数表 Hook

`ur::vmt_hook` 提供了一种专门用于 Hook C++ 对象虚函数表（VMT/VTable）的机制。通过替换虚函数表中的函数指针，可以重定向一个或多个虚函数的调用，同时仍然能够调用原始的虚函数实现。

这对于修改或扩展一个类的特定虚函数行为非常有用，尤其是在处理复杂的类继承体系时。

## 核心特性

- **面向对象**: API 设计直观，直接对类实例进行操作。
- **多重 Hook**: 支持对同一个虚函数表中的多个不同虚函数进行 Hook。
- **Hook 链**: 与 `inline_hook` 类似，对同一个虚函数进行多次 Hook 会形成一个调用链。
- **RAII 管理**: `VmtHook` 管理整个虚函数表的 Hook 会话，而 `VmHook` 管理单个虚函数的 Hook。它们的生命周期决定了 Hook 的安装与卸载。
- **调用原始函数**: `VmHook` 提供了 `get_original` 方法来获取并调用原始的虚函数。

## API 概览

### `ur::VmtHook`

管理一个对象实例的整个虚函数表 Hook 的主类。

#### 构造函数

```cpp
explicit VmtHook(void* instance);
```

- `instance`: 指向要 Hook 的类实例的指针。

```cpp
explicit VmtHook(void** vmt_address);
```

- `vmt_address`: 指向虚函数表（VMT）的指针。这为直接通过虚函数表地址进行 Hook 提供了另一种方式。

#### `hook_method(std::size_t index, void* hook_function)`

Hook 虚函数表中的一个特定函数。

- `index`: 虚函数在虚函数表中的索引（从 0 开始）。
- `hook_function`: 用于替换原始虚函数的新的函数指针。
- **返回值**: 返回一个 `std::unique_ptr<VmHook>`，代表这个特定的 Hook。如果 Hook 失败，则返回 `nullptr`。

### `ur::VmHook`

代表一个被 Hook 的单个虚函数。

#### `get_original<T>()`

获取原始虚函数的函数指针，以便在 Hook 函数中调用它。

- `T`: 原始虚函数的函数指针类型。

#### `enable()`, `disable()`, `unhook()`

与 `inline_hook` 中的方法功能相同，用于控制单个虚函数 Hook 的状态。

## 使用示例

### 1. 基本虚函数 Hook

这个例子展示了如何 Hook 一个类的虚函数，并在 Hook 函数中调用原始实现。

```cpp
#include <ur/vmt_hook.h>
#include <iostream>
#include <memory>

// 目标类
class Calculator {
public:
    virtual int calculate(int a, int b) {
        return a + b;
    }
};

// 全局的 VmHook 句柄，以便在 Hook 函数中访问
std::unique_ptr<ur::VmHook> g_vm_hook;

// Hook 函数，签名需要匹配原始函数（并额外接收一个 this 指针）
int hooked_calculate(Calculator* self, int a, int b) {
    std::cout << "Hooked calculate called." << std::endl;
    // 获取原始函数指针
    auto original_func = g_vm_hook->get_original<int (*)(Calculator*, int, int)>();
    // 调用原始函数
    int original_result = original_func(self, a, b);
    // 修改结果
    return original_result * 10;
}

void vmt_hook_example() {
    Calculator calc;
    Calculator* calc_ptr = &calc; // 必须使用指针以确保虚函数调用

    ur::VmtHook vmt_hook(calc_ptr);

    // Hook `calculate` 函数，它在虚函数表中的索引为 0
    g_vm_hook = vmt_hook.hook_method(0, reinterpret_cast<void*>(&hooked_calculate));

    if (g_vm_hook) {
        std::cout << "VMT Hook successful." << std::endl;
        // 原始结果: 5 + 3 = 8
        // Hook 后结果: 8 * 10 = 80
        int result = calc_ptr->calculate(5, 3);
        std::cout << "Result: " << result << std::endl;
    }

    // 当 g_vm_hook 和 vmt_hook 离开作用域时，Hook 会被自动移除
}
```

### 2. Hook 链

与 `inline_hook` 类似，对同一个虚函数进行多次 Hook 会形成一个调用链。

```cpp
#include <ur/vmt_hook.h>
#include <iostream>
#include <memory>

// ... (Calculator 类定义同上)

std::unique_ptr<ur::VmHook> g_hook_a, g_hook_b;

// Hook A: 加法
int hook_a(Calculator* self, int a, int b) {
    auto original = g_hook_a->get_original<int (*)(Calculator*, int, int)>();
    return original(self, a, b) + 10;
}

// Hook B: 乘法
int hook_b(Calculator* self, int a, int b) {
    auto original = g_hook_b->get_original<int (*)(Calculator*, int, int)>();
    return original(self, a, b) * 2;
}

void vmt_chain_hook_example() {
    Calculator calc;
    ur::VmtHook vmt_hook(&calc);

    // 先安装 Hook A
    g_hook_a = vmt_hook.hook_method(0, reinterpret_cast<void*>(&hook_a));
    // 再安装 Hook B
    g_hook_b = vmt_hook.hook_method(0, reinterpret_cast<void*>(&hook_b));

    // 调用顺序: hook_b -> hook_a -> original_calculate
    // 计算过程: ((5 + 3) + 10) * 2 = 36
    int result = calc.calculate(5, 3);
    std::cout << "Chained VMT hook result: " << result << std::endl;

    // 卸载 Hook B
    g_hook_b.reset();
    // 剩下 Hook A
    // 计算过程: (5 + 3) + 10 = 18
    result = calc.calculate(5, 3);
    std::cout << "After unhooking B: " << result << std::endl;
}
```
