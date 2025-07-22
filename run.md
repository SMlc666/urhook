# Shadowhook 运行机制总结

`shadowhook` 是一个针对 Android 平台的低级动态链接库（或框架），专注于函数 Hooking 或代码注入。其核心运行机制围绕着对目标函数指令的修改，并通过精密的模块协作来确保 Hook 的稳定性、安全性和灵活性。

## 核心概念

1.  **Inline Hook**: 通过直接修改目标函数内存中的指令，使其跳转到 Hook 函数。
2.  **跳板 (Trampoline)**: 一段动态生成的代码，用于：
    *   保存原始指令的备份。
    *   在 Hook 函数执行完毕后，跳转回原始函数的剩余部分。
3.  **原子操作**: 在修改指令时，尽可能使用原子操作，确保写入的完整性。
4.  **缓存刷新**: 修改代码段后，必须刷新 CPU 的指令缓存和数据缓存，以确保 CPU 执行的是最新的指令。
5.  **Hook 模式**:
    *   **UNIQUE 模式**: 每个目标函数只允许一个 Hook。
    *   **SHARED 模式**: 允许多个 Hook 同时作用于同一个目标函数，通过 `sh_hub` 管理 Hook 链。

## 模块协作与运行流程

`shadowhook` 的运行机制可以分解为以下几个主要模块及其协作流程：

### 1. 初始化 (`shadowhook.c` & `sh_task.c`)

*   **`shadowhook_init()`**: 库的入口点，负责初始化所有内部子模块，包括错误码管理、信号处理、安全机制、任务管理、链接器功能等。
*   **`sh_task_init()`**: 注册动态链接库加载/卸载的回调函数 (`sh_task_dl_init_pre`, `sh_task_dl_fini_post`)，以便在库加载时自动处理处于 `PENDING` 状态的 Hook 任务，并在库卸载时清理资源。

### 2. Hook 任务管理 (`sh_task.c`)

*   **`sh_task_t` 结构体**: 封装了一个 Hook 操作的所有信息，包括目标库/符号名、Hook 函数地址、原始函数地址存储位置、Hook 状态等。
*   **任务创建**:
    *   `sh_task_create_by_target_addr()`: 根据目标函数地址创建 Hook 任务。
    *   `sh_task_create_by_sym_name()`: 根据库名和符号名创建 Hook 任务。
*   **`sh_task_hook()`**: Hook 任务的执行入口。
    *   **符号查找**: 如果是按符号名 Hook，会调用 `sh_linker_get_dlinfo_by_sym_name()` 来查找目标函数的实际内存地址。
    *   **PENDING 状态**: 如果目标库尚未加载，任务会进入 `PENDING` 状态，等待库加载后通过回调机制自动完成 Hook。
    *   **委托 Hook 执行**: 调用 `sh_switch_hook()` 来执行实际的指令修改。
    *   **任务队列管理**: 将 Hook 任务添加到内部队列，并更新未完成任务计数。
    *   **记录**: 调用 `sh_recorder_add_hook()` 记录 Hook 操作结果。

### 3. Hook 执行与指令修改 (`sh_switch.c` & `arch/arm64/sh_inst.c` & `common/sh_util.c`)

*   **`sh_switch_hook()`**: 根据 `UNIQUE` 或 `SHARED` 模式，选择不同的 Hook 策略。
    *   **`UNIQUE` 模式**: 直接修改目标函数，只允许一个 Hook。
    *   **`SHARED` 模式**: 如果目标函数已被 Hook，则将新的 Hook 函数添加到 `sh_hub` 管理的代理链中；否则，修改目标函数并初始化 `sh_hub`。
*   **`sh_inst_hook()` (核心指令修改)**:
    1.  **分配“enter”跳板**: 调用 `sh_enter_alloc()` 分配一块可执行内存作为“enter”跳板。
    2.  **内存权限修改**: 调用 `sh_util_mprotect()` 将目标函数所在内存页的权限修改为 `PROT_READ | PROT_WRITE | PROT_EXEC`。
    3.  **备份原始指令**: `memcpy()` 将目标函数起始处的原始指令备份到 `sh_inst_t` 结构体中的 `backup` 缓冲区。
    4.  **指令重写到“enter”跳板**:
        *   遍历备份的原始指令。
        *   对于每条指令，调用 `sh_a64_rewrite()` (在 `arch/arm64/sh_a64.c` 中实现) 生成重写后的 ARM64 指令序列。
        *   将重写后的指令写入“enter”跳板。
        *   在“enter”跳板末尾，生成一条绝对跳转指令 (`sh_a64_absolute_jump_with_ret`)，使其跳转回原始函数中被 Hook 掉的指令之后的部分。
    5.  **写入跳转指令到目标函数头部**:
        *   生成一条绝对跳转指令 (`sh_a64_absolute_jump_with_br`)，使其跳转到 Hook 函数（或 `sh_hub` 的入口）。
        *   **写入方式**: 优先使用 `__atomic_store_n` 进行原子写入（针对 4、8、16 字节且对齐的情况），否则回退到 `memcpy`。这个写入操作由 `sh_util_write_inst()` 完成。
    6.  **缓存刷新**: 调用 `sh_util_clear_cache()` 刷新 CPU 的指令缓存和数据缓存。
    7.  **保存原始函数地址**: 将“enter”跳板的地址保存到 `orig_addr`，供 Hook 函数调用原始功能。

### 3.1 短函数 Hook 策略

对于函数体过小，不足以容纳常规跳转指令（ARM64下为16字节）的“短函数”，`shadowhook` 采用了一种灵活的回退策略：

1.  **尝试使用“exit”跳板 (`sh_inst_hook_with_exit`)**:
    *   **备份长度**: 只备份 **4** 字节的原始指令。
    *   **跳转方式**: 在目标函数头部写入一条 **相对跳转指令 (`B` 指令)**，跳转到一个被称为 **“exit”** 的跳板。
    *   **“exit”跳板**: 这个跳板位于目标函数附近（`B`指令的跳转范围，ARM64下为 +/-128MB），它包含一条 **绝对跳转指令**，最终跳转到真正的 Hook 函数。
    *   **优点**: 只需要4字节空间，能 Hook 非常短的函数。
    *   **缺点**: `B`指令的跳转范围有限，可能找不到合适的位置分配“exit”跳板。

2.  **回退到常规模式 (`sh_inst_hook_without_exit`)**:
    *   **触发条件**: 如果 `sh_inst_hook_with_exit` 失败（例如找不到合适的内存来分配“exit”跳板）。
    *   **备份长度**: 备份 **16** 字节的原始指令。
    *   **跳转方式**: 在目标函数头部写入一条 **绝对跳转指令**（通过 `LDR PC, [PC, #offset]` 实现），直接跳转到 Hook 函数。
    *   **限制**: 要求目标函数的代码长度必须 **大于等于16字节**，否则会返回 `SHADOWHOOK_ERRNO_HOOK_SYMSZ` 错误。

通过这种“优先尝试短跳转，失败则回退”的策略，`shadowhook` 在保证兼容性的同时，也尽可能地提高了对短函数的 Hook 成功率。

### 4. Unhook 机制 (`sh_switch.c` & `arch/arm64/sh_inst.c`)

*   **`sh_task_unhook()`**: 从任务队列中移除 Hook 任务。
*   **`sh_switch_unhook()`**: 根据 Hook 模式执行 Unhook 操作。
    *   **`UNIQUE` 模式**: 直接调用 `sh_inst_unhook()` 恢复原始指令。
    *   **`SHARED` 模式**: 从 `sh_hub` 中移除 Hook 代理。如果 Hook 代理链为空，则调用 `sh_inst_unhook()` 恢复原始指令。
*   **`sh_inst_unhook()`**:
    1.  **验证**: 检查目标函数头部的指令是否与 Hook 时写入的跳转指令一致。
    2.  **恢复原始指令**: 调用 `sh_util_write_inst()` 将备份的原始指令写回目标函数头部。
    3.  **释放资源**: 释放“enter”跳板和“exit”跳板的内存。

### 5. 动态链接库操作 (`shadowhook.c` & `xdl.h`)

*   `shadowhook` 封装了 `xdl` 库，提供了 `shadowhook_dlopen()`、`shadowhook_dlsym()` 等函数，用于加载/卸载动态库和查找符号。这对于通过符号名进行 Hook 是必不可少的。

### 6. 错误处理与日志 (`sh_errno.c` & `sh_log.c`)

*   库内部有完善的错误码和错误信息管理。
*   提供日志功能，方便调试和问题排查。

## 总结流程图

```mermaid
graph TD
    A[shadowhook_init()] --> B{初始化各模块};
    B --> C{注册DL加载/卸载回调};

    D[shadowhook_hook_xxx()] --> E{创建sh_task_t任务};
    E --> F{sh_task_hook()};

    F --> G{查找目标函数地址 (sh_linker)};
    G -- 未加载 --> H{任务PENDING};
    H -- 库加载 --> F;

    G -- 已加载 --> I{sh_switch_hook()};
    I -- UNIQUE模式 --> J{sh_switch_hook_unique()};
    I -- SHARED模式 --> K{sh_switch_hook_shared()};

    J --> L{sh_inst_hook()};
    K --> L;

    L --> M{分配"enter"跳板 (sh_enter_alloc)};
    L --> N{修改内存权限 (sh_util_mprotect)};
    L --> O{备份原始指令 (memcpy)};
    L --> P{重写原始指令到"enter"跳板 (sh_a64_rewrite)};
    L --> Q{生成跳转回原始函数指令 (sh_a64_absolute_jump_with_ret)};
    L --> R{写入跳转指令到目标函数头部 (sh_util_write_inst)};
    L --> S{刷新缓存 (sh_util_clear_cache)};
    L --> T{保存原始函数地址};

    U[shadowhook_unhook()] --> V{sh_task_unhook()};
    V --> W{sh_switch_unhook()};
    W --> X{sh_inst_unhook()};
    X --> Y{验证Hook状态};
    X --> Z{恢复原始指令 (sh_util_write_inst)};
    X --> AA{释放跳板内存};
```
