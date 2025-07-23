#include "ur/elf_parser.h"
#include <gtest/gtest.h>
#include <dlfcn.h>
#include <string>

// A base fixture providing common functionality for setting up a parser for a given library.
class ElfParserTestBase : public ::testing::Test {
protected:
    void SetupForLib(const char* libname, const char* any_symbol_name) {
        // Use RTLD_NOW to ensure all symbols, including IFUNC, are fully resolved at load time.
        handle = dlopen(libname, RTLD_NOW);
        if (handle == nullptr) {
            FAIL() << "Failed to load " << libname << ": " << dlerror();
        }

        // To get the library's base address, we need to find the address of any symbol within it.
        void* any_symbol = dlsym(handle, any_symbol_name);
        if (any_symbol == nullptr) {
            FAIL() << "Failed to find symbol '" << any_symbol_name << "' to determine base address in " << libname;
        }

        Dl_info dl_info;
        if (dladdr(any_symbol, &dl_info) == 0) {
            FAIL() << "dladdr failed for " << libname;
        }
        uintptr_t base_address = reinterpret_cast<uintptr_t>(dl_info.dli_fbase);
        ASSERT_NE(base_address, 0);

        parser = std::make_unique<ur::elf_parser::ElfParser>(base_address);
        ASSERT_TRUE(parser->parse());
    }

    void TearDown() override {
        if (handle != nullptr) {
            dlclose(handle);
        }
    }

    // Helper function to verify that dlsym and our ElfParser find the same address for a symbol.
    void VerifySymbol(const std::string& symbol_name) {
        SCOPED_TRACE("Verifying symbol: " + symbol_name);

        void* symbol_dlsym = dlsym(handle, symbol_name.c_str());
        ASSERT_NE(symbol_dlsym, nullptr) << "dlsym failed for " << symbol_name;

        uintptr_t symbol_elfparser = parser->find_symbol(symbol_name);
        ASSERT_NE(symbol_elfparser, 0) << "ElfParser failed for " << symbol_name;

        EXPECT_EQ(reinterpret_cast<uintptr_t>(symbol_dlsym), symbol_elfparser);
    }

    void* handle = nullptr;
    std::unique_ptr<ur::elf_parser::ElfParser> parser;
};

// Test fixture specifically for libc.so
class ElfParserLibcTest : public ElfParserTestBase {
protected:
    void SetUp() override {
        SetupForLib("libc.so", "strcmp");
    }
};

TEST_F(ElfParserLibcTest, FindFunctionSymbols) {
    VerifySymbol("strcmp");
    VerifySymbol("strlen");
    VerifySymbol("memcpy");
    VerifySymbol("printf");
}

TEST_F(ElfParserLibcTest, FindDataSymbols) {
    // Verify that the parser can find global data symbols, not just functions.
    VerifySymbol("stdin");
    VerifySymbol("stdout");
    VerifySymbol("stderr");
}

TEST_F(ElfParserLibcTest, FindNonExistentSymbol) {
    uintptr_t non_existent_symbol = parser->find_symbol("this_symbol_is_so_non_existent_it_has_its_own_zip_code");
    EXPECT_EQ(non_existent_symbol, 0);
}

// Test fixture specifically for the math library, libm.so
class ElfParserLibmTest : public ElfParserTestBase {
protected:
    void SetUp() override {
        SetupForLib("libm.so", "cos");
    }
};

TEST_F(ElfParserLibmTest, FindMathFunctionSymbols) {
    VerifySymbol("cos");
    VerifySymbol("sin");
    VerifySymbol("sqrt");
    VerifySymbol("log10");
}