# `ur::assembler` & `ur::jit` - 汇编器与即时编译器

`ur::assembler` 和 `ur::jit` 是 UrHook 的底层核心组件，它们提供了在运行时动态生成和执行 AArch64 机器码的能力。`assembler` 负责将指令转换为机器码，而 `jit` 则负责将这些机器码放入可执行内存中。

## `ur::assembler` - A64 汇编器

`Assembler` 类提供了一套流畅的 API，用于生成 AArch64 指令。

### 核心特性

- **全面的指令支持**: 支持数据处理、分支、加载/存储、浮点和 NEON 等多种指令。
- **伪指令**: 提供了如 `gen_abs_jump`（绝对跳转）、`gen_load_address`（加载任意地址）等高级伪指令，简化了常见模式的编码。
- **自动地址管理**: 构造时传入一个起始地址，汇编器会自动处理分支指令的偏移计算。

### API 概览 (`ur::assembler::Assembler`)

#### 构造函数

```cpp
explicit Assembler(uintptr_t start_address = 0);
```

- `start_address`: 汇编代码的虚拟起始地址，用于计算 PC 相关指令的偏移。

#### 主要方法

- **数据处理**: `add`, `sub`, `mov`, `and_`, `orr`, `eor`, `cmp`, `cmn`, etc.
- **分支**: `b`, `bl`, `br`, `blr`, `ret`, `cbz`, `cbnz`, `tbz`, `tbnz`.
- **加载/存储**: `ldr`, `str`, `ldp`, `stp`, `ldr_literal`, `adr`, `adrp`.
- **浮点/NEON**: `fadd`, `fsub`, `fmul`, `fdiv`, `fmov`, `scvtf`, `fcvtzs`, `neon_add`, `neon_mul`, etc.
- **系统指令**: `svc`, `nop`, `mrs`, `msr`.

#### 获取代码

```cpp
const std::vector<uint32_t>& get_code() const;
```

返回已生成的机器码（以 `uint32_t` 数组形式）。

## `ur::jit` - 即时编译器

`Jit` 类继承自 `Assembler`，并增加了将生成的代码写入可执行内存的功能。

### API 概览 (`ur::jit::Jit`)

#### `finalize<T>(uintptr_t hint = 0)`

将汇编代码编译（写入可执行内存）并返回一个指向该代码的函数指针。

- `T`: 函数指针的类型，例如 `int(*)()`。
- `hint`: (可选) 建议的内存分配地址。
- **返回值**: 一个可直接调用的函数指针。

#### `release()`

释放 JIT 生成的代码内存的所有权，并返回指向该内存的指针。调用者需要手动使用 `munmap` 释放内存。

#### `get_code_size()`

获取生成的代码的字节大小。

## 使用示例

### 1. JIT 一个简单的 "add" 函数

```cpp
#include <ur/jit.h>
#include <iostream>

// 定义函数指针类型
using AddFunc = int (*)(int, int);

void jit_add_example() {
    ur::jit::Jit jit;

    // aarch64 调用约定: a 在 w0, b 在 w1, 返回值在 w0
    jit.add(ur::assembler::Register::W0, ur::assembler::Register::W0, ur::assembler::Register::W1);
    jit.ret();

    // 编译并获取函数指针
    AddFunc func = jit.finalize<AddFunc>();

    if (func) {
        int result = func(15, 27);
        std::cout << "JIT-compiled AddFunc(15, 27) result: " << result << std::endl; // 预期: 42
    }
}
```

### 2. JIT 调用外部函数

这个例子展示了如何 JIT 生成调用 `printf` 的代码。

```cpp
#include <ur/jit.h>
#include <cstdio> // for printf

// 定义函数指针类型
using PrintfFunc = int (*)(const char*, ...);
using JitPrintfCaller = void (*)();

void jit_printf_example() {
    ur::jit::Jit jit;
    const char* format_string = "Hello from JIT! Value: %d\n";

    // 保存栈帧
    jit.stp(ur::assembler::Register::FP, ur::assembler::Register::LR, ur::assembler::Register::SP, -16, true);

    // 加载参数
    jit.gen_load_address(ur::assembler::Register::X0, reinterpret_cast<uintptr_t>(format_string));
    jit.mov(ur::assembler::Register::W1, 123); // 第二个参数

    // 调用 printf
    jit.gen_abs_call(reinterpret_cast<uintptr_t>(&printf));

    // 恢复栈帧并返回
    jit.ldp(ur::assembler::Register::FP, ur::assembler::Register::LR, ur::assembler::Register::SP, 16, true);
    jit.ret();

    JitPrintfCaller func = jit.finalize<JitPrintfCaller>();
    if (func) {
        func(); // 执行 JIT 代码
    }
}
```

### 3. 将 JIT 代码用作 Hook 的 Detour

JIT 的一个强大用途是动态创建一个复杂的 Detour 函数，用于 `inline_hook`。

```cpp
#include <ur/jit.h>
#include <ur/inline_hook.h>
#include <iostream>

int original_function() { return 10; }
ur::inline_hook::Hook* g_hook_for_jit_detour;

void jit_detour_example() {
    ur::jit::Jit jit;
    // Detour 逻辑: 调用原始函数，然后将结果乘以 5
    jit.stp(ur::assembler::Register::FP, ur::assembler::Register::LR, ur::assembler::Register::SP, -16, true);
    // 调用原始函数 (通过 call_original)
    jit.gen_load_address(ur::assembler::Register::X0, reinterpret_cast<uintptr_t>(&g_hook_for_jit_detour));
    jit.ldr(ur::assembler::Register::X16, ur::assembler::Register::X0, offsetof(ur::inline_hook::Hook, call_original_));
    jit.blr(ur::assembler::Register::X16);
    // 返回值在 W0, 乘以 5
    jit.mov(ur::assembler::Register::W1, 5);
    jit.mul(ur::assembler::Register::W0, ur::assembler::Register::W0, ur::assembler::Register::W1);
    jit.ldp(ur::assembler::Register::FP, ur::assembler::Register::LR, ur::assembler::Register::SP, 16, true);
    jit.ret();

    auto detour = jit.finalize<ur::inline_hook::Hook::Callback>();

    ur::inline_hook::Hook hook(reinterpret_cast<uintptr_t>(&original_function), detour);
    g_hook_for_jit_detour = &hook;

    // 原始结果: 10
    // Hook 后结果: 10 * 5 = 50
    std::cout << "Result after JIT detour: " << original_function() << std::endl;
}
```