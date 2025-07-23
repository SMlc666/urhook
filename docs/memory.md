# `ur::memory` - 内存操作工具

`ur::memory` 命名空间提供了一组底层、跨平台的静态函数，用于直接对内存进行读取、写入和修改保护属性。这些工具是实现 Hook 和 JIT 功能的基础。

## 核心特性

- **原子性操作**: 内部使用 `process_vm_readv` 和 `process_vm_writev`（如果可用），以提供更安全、更原子的内存操作。
- **缓存刷新**: 提供了刷新指令缓存的函数，这对于修改或生成可执行代码至关重要。
- **简洁的 API**: 提供简单、直接的函数调用，隐藏了底层系统调用的复杂性。

## API 概览

### `read(uintptr_t address, void* buffer, size_t size)`

从指定内存地址读取数据。

- `address`: 要读取的源地址。
- `buffer`: 用于存放读取数据的目标缓冲区。
- `size`: 要读取的字节数。
- **返回值**: `true` 表示成功，`false` 表示失败。

### `write(uintptr_t address, const void* buffer, size_t size)`

向指定内存地址写入数据。

- `address`: 要写入的目标地址。
- `buffer`: 包含要写入数据的源缓冲区。
- `size`: 要写入的字节数。
- **返回值**: `true` 表示成功，`false` 表示失败。

### `protect(uintptr_t address, size_t size, int prot)`

修改指定内存区域的保护属性。

- `address`: 目标内存区域的起始地址。
- `size`: 内存区域的大小。
- `prot`: 新的保护属性，使用 `sys/mman.h` 中定义的常量，如 `PROT_READ`, `PROT_WRITE`, `PROT_EXEC` 的组合。
- **返回值**: `true` 表示成功，`false` 表示失败。

### `flush_instruction_cache(uintptr_t address, size_t size)`

刷新指定内存区域的指令缓存。在修改了可执行代码后，必须调用此函数以确保 CPU 执行的是最新的指令。

- `address`: 目标内存区域的起始地址。
- `size`: 内存区域的大小。

## 使用示例

### 1. 读写变量

```cpp
#include <ur/memory.h>
#include <iostream>
#include <string>

void read_write_example() {
    std::string message = "Hello, World!";
    uintptr_t address = reinterpret_cast<uintptr_t>(&message[0]);

    // 读取原始消息
    char buffer[14] = {0};
    ur::memory::read(address, buffer, message.size());
    std::cout << "Original message: " << buffer << std::endl;

    // 修改消息
    const char* new_message = "Hello, UrHook!";
    ur::memory::write(address, new_message, strlen(new_message) + 1);
    
    std::cout << "Modified message: " << message << std::endl;
}
```

### 2. 使数据段可执行

这个例子展示了如何将一块数据内存标记为可执行，然后“执行”它（在这个例子中是象征性的）。

```cpp
#include <ur/memory.h>
#include <iostream>
#include <sys/mman.h> // For PROT_EXEC

// 假设这是一段机器码: mov w0, #42; ret;
uint32_t machine_code[] = { 0xd2800540, 0xd65f03c0 };

void make_executable_example() {
    uintptr_t code_addr = reinterpret_cast<uintptr_t>(machine_code);
    size_t code_size = sizeof(machine_code);

    // 1. 修改内存保护，增加执行权限
    if (ur::memory::protect(code_addr, code_size, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        std::cout << "Memory is now executable." << std::endl;

        // 2. 刷新指令缓存
        ur::memory::flush_instruction_cache(code_addr, code_size);

        // 3. 将地址转换为函数指针并调用
        auto func = reinterpret_cast<int(*)()>(code_addr);
        int result = func();
        std::cout << "Executed code returned: " << result << std::endl; // 预期: 42

        // 恢复原始权限
        ur::memory::protect(code_addr, code_size, PROT_READ | PROT_WRITE);
    } else {
        std::cerr << "Failed to make memory executable." << std::endl;
    }
}
```
