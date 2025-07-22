#include "gtest/gtest.h"
#include "ur/memory.h"
#include <sys/mman.h>
#include <unistd.h>

namespace ur::memory {

    TEST(MemoryTest, ReadAndWrite) {
        const size_t size = 128;
        void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        ASSERT_NE(p, MAP_FAILED);

        // 写入数据
        uint8_t write_buffer[size];
        for (size_t i = 0; i < size; ++i) {
            write_buffer[i] = static_cast<uint8_t>(i);
        }
        EXPECT_TRUE(write(reinterpret_cast<uintptr_t>(p), write_buffer, size));

        // 读取数据
        uint8_t read_buffer[size];
        EXPECT_TRUE(read(reinterpret_cast<uintptr_t>(p), read_buffer, size));

        // 验证数据
        for (size_t i = 0; i < size; ++i) {
            EXPECT_EQ(read_buffer[i], write_buffer[i]);
        }

        munmap(p, size);
    }

    TEST(MemoryTest, Protect) {
        const size_t size = getpagesize();
        void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        ASSERT_NE(p, MAP_FAILED);

        // 设置为只读
        EXPECT_TRUE(protect(reinterpret_cast<uintptr_t>(p), size, PROT_READ));

        // 尝试写入（由于我们无法捕获段错误，因此这里只是一个象征性的测试）
        // 在真实的场景中，这应该会导致一个段错误
        // 我们只能测试 protect 函数的返回值

        // 恢复为可读写
        EXPECT_TRUE(protect(reinterpret_cast<uintptr_t>(p), size, PROT_READ | PROT_WRITE));

        // 现在应该可以写入了
        uint8_t value = 0xAB;
        EXPECT_TRUE(write(reinterpret_cast<uintptr_t>(p), &value, sizeof(value)));

        uint8_t read_value;
        EXPECT_TRUE(read(reinterpret_cast<uintptr_t>(p), &read_value, sizeof(read_value)));
        EXPECT_EQ(read_value, value);

        munmap(p, size);
    }

    TEST(MemoryTest, ProtectFail) {
        // 尝试保护一个无效的地址
        EXPECT_FALSE(protect(0, getpagesize(), PROT_READ));
    }

} // namespace ur::memory