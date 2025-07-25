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

    // Find the base address of libc.so
    uintptr_t libc_base = 0;
    for (const auto& map : maps) {
        if (map.get_path().find("libc.so") != std::string::npos) {
            if (libc_base == 0 || map.get_start() < libc_base) {
                libc_base = map.get_start();
            }
        }
    }
    ASSERT_NE(libc_base, 0) << "Could not find libc.so in memory maps.";

    // Now, create the ELF parser with the correct base address.
    ur::elf_parser::ElfParser elf_parser(libc_base);
    ASSERT_TRUE(elf_parser.parse()) << "Failed to parse ELF for libc.so.";

    // Use the parser to find a known symbol.
    uintptr_t func_addr = elf_parser.find_symbol("fopen");
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
