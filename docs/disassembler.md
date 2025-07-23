# `ur::disassembler` - A64 反汇编器

`ur::disassembler` 提供了一个用于将 AArch64 机器码字节序列解码为人类可读的汇编指令的接口。这在动态分析、代码检查和调试中非常有用。

UrHook 的反汇编器是自研的，不依赖于 Capstone 等第三方库。

## 核心特性

- **零依赖**: 完全自研，不增加额外的库依赖。
- **详细的指令信息**: 解码后的 `Instruction` 结构体不仅包含助记符和操作数字符串，还提供了指令 ID、分组、操作数详情、PC 相对性等丰富信息。
- **与 `assembler` 模块集成**: 操作数和指令的定义（如 `ur::assembler::Register`）与 `assembler` 模块共享，保证了一致性。

## API 概览

### `CreateAArch64Disassembler()`

一个工厂函数，用于创建反汇编器的实例。

```cpp
std::unique_ptr<Disassembler> CreateAArch64Disassembler();
```

- **返回值**: 一个指向 `Disassembler` 接口的 `std::unique_ptr`。

### `ur::disassembler::Disassembler`

反汇编器的主接口类。

#### `Disassemble(uint64_t address, const uint8_t* code, size_t code_size, size_t count)`

执行反汇编操作。

- `address`: 机器码的起始地址。
- `code`: 指向机器码字节序列的指针。
- `code_size`: 机器码的总字节数。
- `count`: 要反汇编的最大指令数。
- **返回值**: 一个 `std::vector<Instruction>`，包含了所有成功解码的指令。

### `ur::disassembler::Instruction`

代表一条解码后的指令。

#### 主要成员

```cpp
struct Instruction {
    uint64_t address;       // 指令的绝对地址
    uint32_t size;          // 指令大小 (总是 4 字节)
    std::vector<uint8_t> bytes; // 指令的原始字节
    InstructionId id;       // 指令的唯一 ID (例如: InstructionId::ADD)
    InstructionGroup group; // 指令所属的分组 (例如: InstructionGroup::JUMP)
    std::string mnemonic;   // 助记符 (例如: "add")
    std::string op_str;     // 操作数字符串 (例如: "x0, x1, #16")
    bool is_pc_relative;    // 是否是 PC 相对寻址指令
    std::vector<Operand> operands; // 解析后的操作数列表
    // ... 其他成员
};
```

### `ur::disassembler::Operand`

代表一个解析后的操作数，可以是寄存器、立即数或内存地址。

## 使用示例

### 1. 反汇编一段由 `Assembler` 生成的代码

这个例子展示了如何将在 `Assembler` 中生成的代码，再通过 `Disassembler` 解码回来，并验证其正确性。

```cpp
#include <ur/disassembler.h>
#include <ur/assembler.h>
#include <iostream>
#include <iomanip>

void disassemble_example() {
    ur::assembler::Assembler assembler(0x10000); // 假设基地址为 0x10000
    
    // 生成一些指令
    assembler.mov(ur::assembler::Register::X0, 123);
    assembler.add(ur::assembler::Register::X1, ur::assembler::Register::X0, 456);
    assembler.b(0x10000 + 8); // 跳转到自己的下一条指令

    const auto& code_buffer = assembler.get_code();
    const uint8_t* code_bytes = reinterpret_cast<const uint8_t*>(code_buffer.data());
    size_t code_size = code_buffer.size() * sizeof(uint32_t);

    // 创建反汇编器
    auto disassembler = ur::disassembler::CreateAArch64Disassembler();
    
    // 执行反汇编
    auto instructions = disassembler->Disassemble(0x10000, code_bytes, code_size, 3);

    // 打印结果
    for (const auto& instr : instructions) {
        std::cout << "0x" << std::hex << instr.address << ": "
                  << instr.mnemonic << " " << instr.op_str << std::endl;
        
        // 检查解码出的指令 ID
        if (instr.id == ur::disassembler::InstructionId::ADD) {
            std::cout << "  ^-- This is an ADD instruction!" << std::endl;
        }
    }
}
```

预期输出:
```
0x10000: mov x0, #123
0x10004: add x1, x0, #456
  ^-- This is an ADD instruction!
0x10008: b #0x10008
```

### 2. 分析指令的操作数

可以进一步深入 `Instruction` 结构，检查具体的操作数。

```cpp
// ... (延续上一个例子)

void analyze_operands_example() {
    // ... (assembler 和 disassembler 设置同上)
    auto instructions = disassembler->Disassemble(/* ... */);

    // 分析 LDR 指令
    ur::assembler::Assembler assembler_ldr(0x20000);
    assembler_ldr.ldr(ur::assembler::Register::X5, ur::assembler::Register::SP, 16);
    // ... 反汇编 ...
    auto ldr_instrs = disassembler->Disassemble(/* ... */);
    const auto& ldr_instr = ldr_instrs[0];

    if (ldr_instr.id == ur::disassembler::InstructionId::LDR) {
        const auto& dest_reg_op = ldr_instr.operands[0];
        const auto& mem_op = ldr_instr.operands[1];

        // 检查目标寄存器
        auto dest_reg = std::get<ur::assembler::Register>(dest_reg_op.value);
        std::cout << "Destination register: X5? " << (dest_reg == ur::assembler::Register::X5) << std::endl;

        // 检查内存操作数
        auto mem = std::get<ur::disassembler::MemOperand>(mem_op.value);
        std::cout << "Base register: SP? " << (mem.base == ur::assembler::Register::SP) << std::endl;
        std::cout << "Displacement: " << mem.displacement << std::endl;
    }
}
```
