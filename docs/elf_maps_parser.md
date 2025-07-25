# `ur::maps_parser` & `ur::elf_parser` - 内存映射与 ELF 解析

这两个模块提供了在运行时检查进程自身内存布局和解析 ELF（Executable and Linkable Format）文件的能力。它们是实现动态符号查找和地址分析的基础。

## `ur::maps_parser` - 进程内存映射解析

`MapsParser` 用于解析 `/proc/self/maps` 文件，这个文件描述了当前进程的内存段信息，包括地址范围、权限和映射的文件（如共享库）。

### 核心特性

- **静态解析方法**: 提供一个简单的静态方法 `parse()` 来获取当前进程的所有内存映射。
- **独立的内存段信息**: 每个内存段都表示为一个独立的 `MapInfo` 对象，不再进行合并。
- **便捷的查找功能**: 提供了 `find_map_by_addr` 和 `find_map_by_path` 等辅助函数，方便快速定位特定的内存区域。

### API 概览

#### `ur::maps_parser::MapsParser`

```cpp
class MapsParser {
public:
    static std::vector<MapInfo> parse();
    static const MapInfo* find_map_by_addr(const std::vector<MapInfo>& maps, uintptr_t addr);
    static const MapInfo* find_map_by_path(const std::vector<MapInfo>& maps, const std::string& path);
};
```

#### `ur::maps_parser::MapInfo`

代表一个内存段的信息。

```cpp
class MapInfo {
public:
    uintptr_t get_start() const;
    uintptr_t get_end() const;
    const std::string& get_perms() const; // e.g., "r-xp"
    const std::string& get_path() const;  // e.g., "/system/lib64/libc.so"
};
```

## `ur::elf_parser` - ELF 文件解析器

`ElfParser` 用于解析已加载到内存中的 ELF 文件（通常是共享库或主程序），主要是为了查找导出符号的地址。

### 核心特性

- **基于内存**: 直接解析内存中的 ELF 镜像，而不是文件系统中的文件。
- **符号查找**: 高效地从 `.dynsym` (动态符号表) 和 `.symtab` (符号表) 中查找符号。
- **缓存**: 内部缓存符号表，后续查找速度更快。

### API 概览 (`ur::elf_parser::ElfParser`)

#### 构造函数

```cpp
explicit ElfParser(uintptr_t base_address);
```

- `base_address`: ELF 文件在内存中的基地址。

#### `parse()`

解析 ELF 头部和程序头，为符号查找做准备。

#### `find_symbol(const std::string& symbol_name)`

在动态符号表和符号表中查找一个符号。

- `symbol_name`: 要查找的符号名称，例如 `"fopen"`。
- **返回值**: 符号的绝对地址。如果未找到，返回 `0`。

## 使用示例

### 1. 查找 `libc.so` 中 `fopen` 函数的地址

这是一个典型的用例：在运行时动态定位一个库函数的位置。新的工作流程需要手动从内存映射中找到库的基地址，然后创建 `ElfParser`。

```cpp
#include <ur/maps_parser.h>
#include <ur/elf_parser.h>
#include <iostream>
#include <dlfcn.h> // 用于与 dlsym 对比验证

void find_fopen_example() {
    // 1. 解析当前进程的内存映射
    auto maps = ur::maps_parser::MapsParser::parse();
    if (maps.empty()) {
        std::cerr << "Failed to parse memory maps." << std::endl;
        return;
    }

    // 2. 遍历 maps，找到 libc.so 的基地址
    // 基地址通常是该库所有内存段中最低的起始地址
    uintptr_t libc_base = 0;
    for (const auto& map : maps) {
        if (map.get_path().find("libc.so") != std::string::npos) {
            if (libc_base == 0 || map.get_start() < libc_base) {
                libc_base = map.get_start();
            }
        }
    }

    if (libc_base == 0) {
        std::cerr << "Failed to find libc.so in memory maps." << std::endl;
        return;
    }

    // 3. 使用基地址创建 ElfParser 实例
    ur::elf_parser::ElfParser parser(libc_base);
    if (!parser.parse()) {
        std::cerr << "Failed to parse ELF for libc.so." << std::endl;
        return;
    }

    // 4. 使用 ElfParser 查找符号地址
    uintptr_t fopen_addr = parser.find_symbol("fopen");

    if (fopen_addr != 0) {
        std::cout << "Found fopen via UrHook: 0x" << std::hex << fopen_addr << std::endl;

        // 5. (验证) 使用 dlsym 进行对比
        void* handle = dlopen("libc.so", RTLD_LAZY);
        void* fopen_dlsym = dlsym(handle, "fopen");
        std::cout << "Found fopen via dlsym:  0x" << std::hex << reinterpret_cast<uintptr_t>(fopen_dlsym) << std::endl;
        dlclose(handle);
    } else {
        std::cout << "Failed to find fopen via UrHook." << std::endl;
    }
}
```
