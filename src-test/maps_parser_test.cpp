#include "gtest/gtest.h"
#include "ur/maps_parser.h"
#include <vector>
#include <string>
#include <algorithm>

// A dummy function to have an address within the test executable's code segment.
void dummy_function_for_address_test() {}

TEST(MapsParserTest, ParseAndFindLibcThenFindSymbol) {
    auto maps = ur::maps_parser::MapsParser::parse();
    ASSERT_FALSE(maps.empty());

    // Find the map for libc.so. The exact path can vary.
    const ur::maps_parser::MapInfo* libc_map_info = nullptr;
    for (const auto& map : maps) {
        // Use find instead of exact match because path could be e.g. /apex/com.android.runtime/lib64/bionic/libc.so
        if (map.get_path().find("libc.so") != std::string::npos) {
            libc_map_info = &map;
            break;
        }
    }
    ASSERT_NE(libc_map_info, nullptr) << "Could not find libc.so in memory maps.";

    // The executable segment should be readable and executable.
    EXPECT_TRUE(libc_map_info->get_perms().find('r') != std::string::npos);
    EXPECT_TRUE(libc_map_info->get_perms().find('x') != std::string::npos);

    // Now, try to get the ELF parser.
    auto* elf_parser = libc_map_info->get_elf_parser();
    ASSERT_NE(elf_parser, nullptr) << "Failed to get ELF parser for libc.so.";

    // Use the parser to find a known symbol.
    uintptr_t func_addr = elf_parser->find_symbol("fopen");
    EXPECT_NE(func_addr, 0) << "Failed to find symbol 'fopen' in libc.so.";
}

TEST(MapsParserTest, FindByAddress) {
    auto maps = ur::maps_parser::MapsParser::parse();
    ASSERT_FALSE(maps.empty());

    // Find the map for the test function itself
    auto func_ptr = reinterpret_cast<std::uintptr_t>(&dummy_function_for_address_test);
    auto* map_info = ur::maps_parser::MapsParser::find_map_by_addr(maps, func_ptr);

    ASSERT_NE(map_info, nullptr);
    EXPECT_TRUE(map_info->get_perms().find('r') != std::string::npos);
    EXPECT_TRUE(map_info->get_perms().find('x') != std::string::npos); // Code should be executable
}
