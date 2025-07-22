# UrHook 项目文档

## 1. 项目概述

UrHook 是一个专注于 aarch64 架构的 Android Hook 库。它旨在提供稳定、高效且用户友好的 API，方便其他项目进行复用。

## 2. 技术栈

- **语言**: C++20
- **构建系统**: Xmake
- **测试框架**: GTest
- **反汇编引擎**: Capstone
- **汇编引擎**: 目前该项目自实现了一套基础的汇编器

## 3. 目录结构

```
.
├── include         # 头文件
├── src             # 源代码
├── src-test        # 测试代码
└── xmake.lua       # 构建脚本
```

## 4. 开发规范

### 4.1. 编码风格

- **命名空间**: 所有代码都必须位于 `ur::文件名` 的命名空间内。
- **代码质量**: 追求现代化、高质量的 C++ 实现，遵循面向对象和模块化的设计原则，保证 API 的健壮与优雅。

## 5. 构建与测试

- **配置模式**:
  - `xmake f -m debug` (调试模式)
  - `xmake f -m release` (发行模式，默认)
- **编译**: `xmake`
- **运行单元测试**: `xmake run` (注意，这里不是使用xmake test运行单元测试)

## 6. API 参考与示例

本节将详细介绍 UrHook 提供的核心 API，并提供使用示例。

### 6.1. `ur::assembler::Assembler` - A64 汇编器

`Assembler` 类提供了一系列方法用于生成 A64 架构的机器码。

**主要功能:**
- 生成各种 A64 指令，包括数据处理、分支、加载/存储、浮点运算、NEON 指令等。
- 支持伪指令，如绝对跳转、绝对调用、加载地址、push/pop 寄存器等。

**构造函数:**
```cpp
explicit Assembler(uintptr_t start_address);
```
- `start_address`: 汇编代码的起始地址。

**常用方法示例:**

#### 6.1.1. 绝对跳转和调用

```cpp
#include <ur/assembler.h>
#include <vector>
#include <iostream>

// 假设目标地址
uintptr_t target_func_address = 0x12345678;

ur::assembler::Assembler ass(0x1000); // 假设从地址 0x1000 开始汇编

// 生成绝对跳转到 target_func_address
ass.gen_abs_jump(target_func_address, ur::assembler::Register::X16);

// 生成绝对调用到 target_func_address
ass.gen_abs_call(target_func_address, ur::assembler::Register::X17);

// 获取生成的机器码
const std::vector<uint32_t>& code = ass.get_code();
// ... 将 code 写入内存并执行
```

#### 6.1.2. 数据处理指令

```cpp
#include <ur/assembler.h>

ur::assembler::Assembler ass(0x2000);

// R0 = R1 + R2
ass.add(ur::assembler::Register::X0, ur::assembler::Register::X1, ur::assembler::Register::X2);

// R0 = R1 - 100
ass.sub(ur::assembler::Register::X0, ur::assembler::Register::X1, 100);

// R0 = 0x123456789ABCDEF0
ass.mov(ur::assembler::Register::X0, 0x123456789ABCDEF0ULL);
```

#### 6.1.3. 加载/存储指令

```cpp
#include <ur/assembler.h>

ur::assembler::Assembler ass(0x3000);

// 从 R1 + 8 的地址加载到 R0
ass.ldr(ur::assembler::Register::X0, ur::assembler::Register::X1, 8);

// 将 R0 的值存储到 R1 + 16 的地址
ass.str(ur::assembler::Register::X0, ur::assembler::Register::X1, 16);
```

### 6.2. `ur::disassembler::Disassembler` - A64 反汇编器

`Disassembler` 接口用于反汇编 A64 机器码。通过 `CreateAArch64Disassembler()` 工厂函数创建实例。

**主要功能:**
- 将机器码反汇编成可读的指令结构。

**工厂函数:**
```cpp
std::unique_ptr<Disassembler> CreateAArch64Disassembler();
```

**`Instruction` 结构体:**
```cpp
struct Instruction {
    uint64_t address;       // 指令地址
    uint32_t size;          // 指令大小 (字节)
    std::vector<uint8_t> bytes; // 指令原始字节
};
```

**`Disassemble` 方法:**
```cpp
virtual std::vector<Instruction> Disassemble(
    uint64_t address,
    const uint8_t* code,
    size_t code_size,
    size_t count
) = 0;
```
- `address`: 开始反汇编的内存地址。
- `code`: 指向机器码字节的指针。
- `code_size`: 机器码的字节大小。
- `count`: 最大反汇编指令数量。

**使用示例:**

```cpp
#include <ur/disassembler.h>
#include <iostream>
#include <vector>

// 假设有一段机器码
std::vector<uint8_t> code_bytes = {
    0x00, 0x00, 0x80, 0xD2, // MOV X0, #0
    0x01, 0x00, 0x80, 0xD2  // MOV X1, #0
};

uint64_t start_address = 0x40000; // 假设代码起始地址

std::unique_ptr<ur::disassembler::Disassembler> disassembler = ur::disassembler::CreateAArch64Disassembler();

std::vector<ur::disassembler::Instruction> instructions = disassembler->Disassemble(
    start_address,
    code_bytes.data(),
    code_bytes.size(),
    2 // 反汇编两条指令
);

for (const auto& instr : instructions) {
    std::cout << "Address: 0x" << std::hex << instr.address
              << ", Size: " << std::dec << instr.size
              << ", Bytes: ";
    for (uint8_t byte : instr.bytes) {
        std::cout << std::hex << (int)byte << " ";
    }
    std::cout << std::endl;
}
```

### 6.3. `ur::inline_hook::Hook` - 行内 Hook

`Hook` 类用于在指定函数地址进行行内 Hook。

**主要功能:**
- 替换目标函数的起始指令，跳转到用户提供的回调函数。
- 提供 `call_original` 方法，允许在回调函数中调用原始函数。

**构造函数:**
```cpp
Hook(uintptr_t target, Callback callback);
```
- `target`: 目标函数的地址。
- `callback`: Hook 触发时执行的回调函数。`Callback` 类型为 `void*`，通常需要强制转换为实际的函数指针类型。

**`call_original` 方法:**
```cpp
template<typename Ret, typename... Args>
Ret call_original(Args... args) const;
```
- `Ret`: 原始函数的返回类型。
- `Args`: 原始函数的参数类型。
- `args`: 传递给原始函数的参数。

**使用示例:**

```cpp
#include <ur/inline_hook.h>
#include <iostream>

// 假设一个原始函数
int original_function(int a, int b) {
    std::cout << "Original function called with: " << a << ", " << b << std::endl;
    return a + b;
}

// Hook 回调函数
void* my_hook_callback(int a, int b) {
    std::cout << "Hook callback called with: " << a << ", " << b << std::endl;

    // 调用原始函数
    ur::inline_hook::Hook* current_hook = nullptr; // 实际使用时需要获取当前 Hook 实例
    // 这里只是一个示例，实际的 call_original 调用方式可能依赖于 Hook 库的内部实现
    // 通常 Hook 库会提供一个机制来获取当前 Hook 实例或直接调用原始函数
    // 假设我们有一个全局或可访问的 Hook 实例
    // int result = current_hook->call_original<int>(a, b);
    // std::cout << "Original function returned: " << result << std::endl;

    // 为了示例完整性，这里直接返回一个值
    return reinterpret_cast<void*>(static_cast<uintptr_t>(a * b));
}

int main() {
    // 创建 Hook 实例
    ur::inline_hook::Hook hook(reinterpret_cast<uintptr_t>(&original_function), reinterpret_cast<ur::inline_hook::Hook::Callback>(&my_hook_callback));

    if (hook.is_valid()) {
        std::cout << "Hook successful!" << std::endl;
        int result = original_function(10, 20); // 调用被 Hook 的函数
        std::cout << "Function call returned: " << result << std::endl;
    } else {
        std::cout << "Hook failed!" << std::endl;
    }

    return 0;
}
```

### 6.4. `ur::jit::Jit` - 即时编译

`Jit` 类继承自 `ur::assembler::Assembler`，用于将汇编器生成的机器码即时编译并写入可执行内存。

**主要功能:**
- 将 `Assembler` 生成的机器码分配到可执行内存区域。
- 提供 `finalize` 方法获取可执行的函数指针。

**构造函数:**
```cpp
explicit Jit(uintptr_t address = 0);
```
- `address`: JIT 代码的起始地址（可选，默认为 0）。

**`finalize` 方法:**
```cpp
template<typename T>
T finalize(uintptr_t hint = 0);
```
- `T`: 目标函数指针类型。
- `hint`: 内存分配的提示地址（可选，默认为 0）。

**使用示例:**

```cpp
#include <ur/jit.h>
#include <iostream>

// 定义一个函数指针类型
typedef int (*MyJitFunc)(int, int);

int main() {
    ur::jit::Jit jit;

    // 汇编一段简单的代码：将两个参数相加并返回
    // 假设参数在 X0 和 X1 中，结果在 X0 中
    jit.add(ur::assembler::Register::X0, ur::assembler::Register::X0, ur::assembler::Register::X1);
    jit.ret(); // 返回

    // 编译并获取函数指针
    MyJitFunc func = jit.finalize<MyJitFunc>();

    if (func) {
        std::cout << "JIT function created successfully." << std::endl;
        int result = func(10, 20);
        std::cout << "JIT function returned: " << result << std::endl; // 应该输出 30
    } else {
        std::cout << "Failed to create JIT function." << std::endl;
    }

    return 0;
}
```

### 6.5. `ur::memory` - 内存操作

`ur::memory` 命名空间提供了一系列用于内存读写、保护和缓存刷新的实用函数。

**主要功能:**
- `read`: 从指定地址读取内存。
- `write`: 向指定地址写入内存。
- `protect`: 修改内存页的保护属性。
- `flush_instruction_cache`: 刷新指令缓存。

**函数签名:**
```cpp
bool read(uintptr_t address, void* buffer, size_t size);
bool write(uintptr_t address, const void* buffer, size_t size);
bool protect(uintptr_t address, size_t size, int prot); // prot: PROT_READ, PROT_WRITE, PROT_EXEC 等
void flush_instruction_cache(uintptr_t address, size_t size);
```

**使用示例:**

```cpp
#include <ur/memory.h>
#include <iostream>
#include <vector>
#include <sys/mman.h> // For PROT_READ, PROT_WRITE, PROT_EXEC

int main() {
    // 假设有一块内存
    std::vector<uint8_t> data = {0x11, 0x22, 0x33, 0x44};
    uintptr_t address = reinterpret_cast<uintptr_t>(data.data());

    // 写入内存
    uint8_t new_byte = 0xAA;
    if (ur::memory::write(address, &new_byte, 1)) {
        std::cout << "Successfully wrote 0xAA to address " << std::hex << address << std::endl;
    } else {
        std::cout << "Failed to write to memory." << std::endl;
    }

    // 读取内存
    uint8_t read_byte;
    if (ur::memory::read(address, &read_byte, 1)) {
        std::cout << "Successfully read 0x" << std::hex << (int)read_byte << " from address " << std::hex << address << std::endl;
    } else {
        std::cout << "Failed to read from memory." << std::endl;
    }

    // 修改内存保护为可读写可执行
    if (ur::memory::protect(address, data.size(), PROT_READ | PROT_WRITE | PROT_EXEC)) {
        std::cout << "Memory protection changed to RWE." << std::endl;
    } else {
        std::cout << "Failed to change memory protection." << std::endl;
    }

    // 刷新指令缓存 (如果修改了可执行代码)
    ur::memory::flush_instruction_cache(address, data.size());

    return 0;
}
```

### 6.6. `ur::mid_hook::MidHook` - 中间 Hook

`MidHook` 类用于在函数内部的特定位置插入 Hook。它会保存 CPU 上下文，执行回调，然后恢复上下文并执行原始指令。

**主要功能:**
- 在函数中间插入 Hook。
- 提供 `CpuContext` 结构体，允许在回调中访问和修改寄存器。

**`CpuContext` 结构体:**
```cpp
struct CpuContext {
    uint64_t gpr[31]; // 通用寄存器 x0-x28, fp(x29), lr(x30)
};
```

**`Callback` 类型:**
```cpp
using Callback = void (*)(CpuContext* context);
```
- `context`: 指向 `CpuContext` 结构体的指针，允许访问和修改寄存器。

**构造函数:**
```cpp
MidHook(uintptr_t target, Callback callback);
```
- `target`: Hook 插入的目标地址。
- `callback`: Hook 触发时执行的回调函数。

**使用示例:**

```cpp
#include <ur/mid_hook.h>
#include <iostream>

// 假设一个目标函数
void target_mid_hook_function(int a, int b) {
    volatile int sum = a + b; // 使用 volatile 防止优化
    std::cout << "Inside target_mid_hook_function. Sum: " << sum << std::endl;
    // 假设我们想在 sum 计算后 Hook
}

// MidHook 回调函数
void my_mid_hook_callback(ur::mid_hook::CpuContext* context) {
    std::cout << "MidHook callback triggered!" << std::endl;
    // 示例：打印 x0 和 x1 寄存器的值
    std::cout << "X0 (arg1): " << context->gpr[0] << std::endl;
    std::cout << "X1 (arg2): " << context->gpr[1] << std::endl;

    // 示例：修改 x0 寄存器的值
    context->gpr[0] = 999;
}

int main() {
    // 找到 target_mid_hook_function 中我们想要 Hook 的位置
    // 这通常需要反汇编目标函数来确定精确的地址
    // 这里只是一个示意性的地址，实际使用时需要精确计算
    uintptr_t hook_address = reinterpret_cast<uintptr_t>(&target_mid_hook_function) + 0x10; // 假设偏移 0x10

    ur::mid_hook::MidHook mid_hook(hook_address, &my_mid_hook_callback);

    if (mid_hook.is_valid()) {
        std::cout << "MidHook successful!" << std::endl;
        target_mid_hook_function(5, 7); // 调用目标函数
    } else {
        std::cout << "MidHook failed!" << std::endl;
    }

    return 0;
}
```

### 6.7. `ur::VmtHook` 和 `ur::VmHook` - 虚表 Hook

`VmtHook` 和 `VmHook` 类用于对 C++ 虚函数表进行 Hook。

**主要功能:**
- `VmtHook`: 用于创建虚表 Hook 的主类，通过实例指针进行初始化。
- `VmHook`: 表示单个虚函数 Hook 的类，通过 `VmtHook::hook_method` 创建。
- `VmHook::get_original`: 获取原始虚函数的指针。

**`VmtHook` 构造函数:**
```cpp
explicit VmtHook(void* instance);
```
- `instance`: 虚函数表所属类的实例指针。

**`VmtHook::hook_method` 方法:**
```cpp
[[nodiscard]] std::unique_ptr<VmHook> hook_method(std::size_t index, void* hook_function);
```
- `index`: 虚函数在虚表中的索引。
- `hook_function`: 替换原始虚函数的 Hook 函数指针。

**`VmHook::get_original` 方法:**
```cpp
template <typename T>
T get_original() const;
```
- `T`: 原始虚函数的函数指针类型。

**使用示例:**

```cpp
#include <ur/vmt_hook.h>
#include <iostream>
#include <memory>

// 假设一个带有虚函数的基类
class MyBaseClass {
public:
    virtual void virtual_method_1() {
        std::cout << "MyBaseClass::virtual_method_1 called." << std::endl;
    }

    virtual int virtual_method_2(int a, int b) {
        std::cout << "MyBaseClass::virtual_method_2 called with: " << a << ", " << b << std::endl;
        return a + b;
    }
};

// Hook 函数
void hooked_method_1() {
    std::cout << "Hooked virtual_method_1 called!" << std::endl;
}

// Hook 函数，并调用原始函数
int hooked_method_2(MyBaseClass* instance, int a, int b) {
    std::cout << "Hooked virtual_method_2 called with: " << a << ", " << b << std::endl;

    // 获取原始函数并调用
    // 注意：这里需要通过 VmHook 实例来获取原始函数
    // 实际使用时，VmHook 实例通常会被保存起来
    // 为了示例，我们假设有一个全局或可访问的 VmHook 实例
    // int result = original_method_2(instance, a, b);
    // std::cout << "Original virtual_method_2 returned: " << result << std::endl;

    // 为了示例完整性，这里直接返回一个值
    return a * b;
}

int main() {
    MyBaseClass obj;

    // 创建 VmtHook 实例
    ur::VmtHook vmt_hook(&obj);

    // Hook 第一个虚函数 (索引 0)
    std::unique_ptr<ur::VmHook> vm_hook_1 = vmt_hook.hook_method(0, reinterpret_cast<void*>(&hooked_method_1));

    // Hook 第二个虚函数 (索引 1)
    // 注意：对于非静态成员函数，Hook 函数的第一个参数通常是类的实例指针
    std::unique_ptr<ur::VmHook> vm_hook_2 = vmt_hook.hook_method(1, reinterpret_cast<void*>(&hooked_method_2));

    if (vm_hook_1 && vm_hook_2) {
        std::cout << "VMT Hook successful!" << std::endl;

        obj.virtual_method_1(); // 调用被 Hook 的虚函数
        int result = obj.virtual_method_2(10, 5); // 调用被 Hook 的虚函数
        std::cout << "virtual_method_2 returned: " << result << std::endl;

        // 调用原始函数 (通过 vm_hook_2 获取)
        // typedef int (MyBaseClass::*OriginalMethod2)(int, int);
        // OriginalMethod2 original_func = vm_hook_2->get_original<OriginalMethod2>();
        // int original_result = (obj.*original_func)(10, 5);
        // std::cout << "Original virtual_method_2 (via get_original) returned: " << original_result << std::endl;

    } else {
        std::cout << "VMT Hook failed!" << std::endl;
    }

    return 0;
}
