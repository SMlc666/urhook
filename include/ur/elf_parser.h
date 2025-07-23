#pragma once

#include <string>
#include <vector>
#include <memory>
#include <elf.h>

namespace ur::elf_parser {

    class ElfHeader {
    public:
        explicit ElfHeader(uintptr_t base_address);
        bool is_valid() const;
        uintptr_t get_program_header_offset() const;
        uintptr_t get_section_header_offset() const;
        uint16_t get_program_header_count() const;
        uint16_t get_section_header_count() const;
        uint16_t get_section_header_string_table_index() const;

    private:
        const Elf64_Ehdr* m_header;
    };

    class ProgramHeader {
    public:
        explicit ProgramHeader(const Elf64_Phdr* phdr);
        uint32_t get_type() const;
        uint64_t get_virtual_address() const;
        uint64_t get_offset() const;
        uint64_t get_flags() const;
        uint64_t get_file_size() const;

    private:
        const Elf64_Phdr* m_phdr;
    };

    class ProgramHeaderTable {
    public:
        ProgramHeaderTable(uintptr_t base_address, const ElfHeader& header);
        const ProgramHeader* find_first_by_type(uint32_t type) const;
        std::vector<ProgramHeader>::const_iterator begin() const;
        std::vector<ProgramHeader>::const_iterator end() const;

    private:
        std::vector<ProgramHeader> m_program_headers;
    };

    class SectionHeader {
    public:
        explicit SectionHeader(const Elf64_Shdr* shdr);
        uint32_t get_type() const;
        uint64_t get_offset() const;
        uint64_t get_size() const;
        std::string get_name(const char* string_table) const;

    private:
        const Elf64_Shdr* m_shdr;
    };

    class ElfParser; // Forward declaration

    class SectionHeaderTable {
    public:
        SectionHeaderTable(uintptr_t sh_table_addr, const ElfHeader& header, ElfParser* parser);
        const SectionHeader* get_section_by_name(const std::string& name) const;
        const SectionHeader* get_section_by_index(uint16_t index) const;
        std::vector<SectionHeader>::const_iterator begin() const;
        std::vector<SectionHeader>::const_iterator end() const;

    private:
        const char* get_string_table() const;
        std::vector<SectionHeader> m_section_headers;
        const char* m_string_table_data = nullptr;
    };

    class ElfParser {
    public:
        explicit ElfParser(uintptr_t base_address);
        bool parse();
        uintptr_t find_symbol(const std::string& symbol_name);
        uintptr_t file_offset_to_memory_addr(uint64_t offset);
        const std::unique_ptr<SectionHeaderTable>& get_section_header_table() const;
        uintptr_t get_load_bias() const;
        uintptr_t get_plt_rel_location() const;
        size_t get_plt_rel_size() const;
        int get_plt_rel_entry_type() const;
        const Elf64_Sym* get_dynamic_symbol_table() const;
        const char* get_dynamic_string_table() const;

    private:
        friend class SectionHeaderTable;
        uintptr_t find_symbol_by_gnu_hash(const std::string& symbol_name);
        uintptr_t find_symbol_by_hash(const std::string& symbol_name);
        uintptr_t find_symbol_in_table(const std::string& symbol_name, const Elf64_Sym* symtab, const char* strtab, size_t sym_count);

        uintptr_t m_base_address;
        uintptr_t m_load_bias = 0;
        std::unique_ptr<ElfHeader> m_header;
        std::unique_ptr<ProgramHeaderTable> m_program_header_table;
        std::unique_ptr<SectionHeaderTable> m_section_header_table;

        const Elf64_Sym* m_dynsym = nullptr;
        const char* m_dynstr = nullptr;
        const uint32_t* m_gnu_hash_table = nullptr;
        const uint32_t* m_hash_table = nullptr;

        const Elf64_Sym* m_symtab = nullptr;
        const char* m_strtab = nullptr;
        size_t m_symtab_count = 0;

        uintptr_t m_plt_rel_location = 0;
        size_t m_plt_rel_size = 0;
        int m_plt_rel_entry_type = 0;
    };
}
