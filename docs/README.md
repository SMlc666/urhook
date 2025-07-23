# UrHook 项目文档

## 1. 项目概述

UrHook 是一个专注于 aarch64 架构的 Android Hook 库。它旨在提供稳定、高效且用户友好的 API，方便其他项目进行复用。

## 2. 技术栈

- **语言**: C++20
- **构建系统**: Xmake
- **测试框架**: GTest
- **反汇编引擎**: 自研
- **汇编引擎**: 自研

## 3. 目录结构

```
.
├── include         # 头文件
├── src             # 源代码
├── src-test        # 测试代码
├── docs            # 文档
└── xmake.lua       # 构建脚本
```

## 4. 构建与测试

- **配置模式**:
  - `xmake f -m debug` (调试模式)
  - `xmake f -m release` (发行模式，默认)
- **编译**: `xmake`
- **运行单元测试**: `xmake run` (此命令会先编译后运行，无需手动执行 `xmake`)

## 5. API 文档

UrHook 的 API 被设计为模块化和可组合的。下面是核心组件的文档链接：

- **[Hooking APIs](./) (即将推出)**
  - **[`inline_hook`](./inline_hook.md)**: 在函数入口进行 Hook。
  - **[`mid_hook`](./mid_hook.md)**: 在函数中间的任意位置进行 Hook。
  - **[`vmt_hook`](./vmt_hook.md)**: 针对 C++ 虚函数表的 Hook。

- **[代码生成与分析](./)**
  - **[`assembler` & `jit`](./assembler_jit.md)**: 动态生成和执行 AArch64 机器码。
  - **[`disassembler`](./disassembler.md)**: 解析 AArch64 机器码。

- **[内存与 ELF 工具](./)**
  - **[`memory`](./memory.md)**: 底层内存读、写、保护等操作。
  - **[`elf_parser` & `maps_parser`](./elf_maps_parser.md)**: 解析进程内存映射和 ELF 文件格式。