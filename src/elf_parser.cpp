#include "ur/elf_parser.h"
#include <vector>

namespace ur::elf_parser {

    // ElfHeader Implementation
    ElfHeader::ElfHeader(uintptr_t base_address)
        : m_header(reinterpret_cast<const Elf64_Ehdr*>(base_address)) {}

    bool ElfHeader::is_valid() const {
        if (m_header == nullptr) return false;
        return memcmp(m_header->e_ident, ELFMAG, SELFMAG) == 0 &&
               m_header->e_ident[EI_CLASS] == ELFCLASS64 &&
               m_header->e_machine == EM_AARCH64;
    }

    uintptr_t ElfHeader::get_program_header_offset() const {
        return m_header->e_phoff;
    }

    uintptr_t ElfHeader::get_section_header_offset() const {
        return m_header->e_shoff;
    }

    uint16_t ElfHeader::get_program_header_count() const {
        return m_header->e_phnum;
    }

    uint16_t ElfHeader::get_section_header_count() const {
        return m_header->e_shnum;
    }

    uint16_t ElfHeader::get_section_header_string_table_index() const {
        return m_header->e_shstrndx;
    }

    // ProgramHeader Implementation
    ProgramHeader::ProgramHeader(const Elf64_Phdr* phdr) : m_phdr(phdr) {}

    uint32_t ProgramHeader::get_type() const {
        return m_phdr->p_type;
    }

    uint64_t ProgramHeader::get_virtual_address() const {
        return m_phdr->p_vaddr;
    }
    
    uint64_t ProgramHeader::get_offset() const {
        return m_phdr->p_offset;
    }

    uint64_t ProgramHeader::get_flags() const {
        return m_phdr->p_flags;
    }

    uint64_t ProgramHeader::get_file_size() const {
        return m_phdr->p_filesz;
    }

    // ProgramHeaderTable Implementation
    ProgramHeaderTable::ProgramHeaderTable(uintptr_t base_address, const ElfHeader& header) {
        auto phdr_offset = header.get_program_header_offset();
        auto phdr_count = header.get_program_header_count();
        auto phdr_size = sizeof(Elf64_Phdr);

        for (uint16_t i = 0; i < phdr_count; ++i) {
            const auto phdr = reinterpret_cast<const Elf64_Phdr*>(base_address + phdr_offset + i * phdr_size);
            m_program_headers.emplace_back(phdr);
        }
    }

    const ProgramHeader* ProgramHeaderTable::find_first_by_type(uint32_t type) const {
        for (const auto& phdr : m_program_headers) {
            if (phdr.get_type() == type) {
                return &phdr;
            }
        }
        return nullptr;
    }

    std::vector<ProgramHeader>::const_iterator ProgramHeaderTable::begin() const {
        return m_program_headers.cbegin();
    }

    std::vector<ProgramHeader>::const_iterator ProgramHeaderTable::end() const {
        return m_program_headers.cend();
    }

    // SectionHeader Implementation
    SectionHeader::SectionHeader(const Elf64_Shdr* shdr) : m_shdr(shdr) {}

    uint32_t SectionHeader::get_type() const {
        return m_shdr->sh_type;
    }

    uint64_t SectionHeader::get_offset() const {
        return m_shdr->sh_offset;
    }

    uint64_t SectionHeader::get_size() const {
        return m_shdr->sh_size;
    }

    std::string SectionHeader::get_name(const char* string_table) const {
        if (string_table == nullptr) {
            return "";
        }
        return std::string(string_table + m_shdr->sh_name);
    }

    // SectionHeaderTable Implementation
    SectionHeaderTable::SectionHeaderTable(uintptr_t sh_table_addr, const ElfHeader& header, ElfParser* parser) {
        auto shdr_count = header.get_section_header_count();
        auto shdr_size = sizeof(Elf64_Shdr);

        for (uint16_t i = 0; i < shdr_count; ++i) {
            const auto shdr = reinterpret_cast<const Elf64_Shdr*>(sh_table_addr + i * shdr_size);
            m_section_headers.emplace_back(shdr);
        }

        auto str_tbl_idx = header.get_section_header_string_table_index();
        if (str_tbl_idx != SHN_UNDEF && str_tbl_idx < m_section_headers.size()) {
            const auto& str_sec_hdr = m_section_headers[str_tbl_idx];
            m_string_table_data = reinterpret_cast<const char*>(parser->file_offset_to_memory_addr(str_sec_hdr.get_offset()));
        }
    }

    const SectionHeader* SectionHeaderTable::get_section_by_name(const std::string& name) const {
        for (const auto& shdr : m_section_headers) {
            if (shdr.get_name(m_string_table_data) == name) {
                return &shdr;
            }
        }
        return nullptr;
    }
    
    const SectionHeader* SectionHeaderTable::get_section_by_index(uint16_t index) const {
        if (index < m_section_headers.size()) {
            return &m_section_headers[index];
        }
        return nullptr;
    }

    std::vector<SectionHeader>::const_iterator SectionHeaderTable::begin() const {
        return m_section_headers.cbegin();
    }

    std::vector<SectionHeader>::const_iterator SectionHeaderTable::end() const {
        return m_section_headers.cend();
    }

    const char* SectionHeaderTable::get_string_table() const {
        return m_string_table_data;
    }

    const std::unique_ptr<SectionHeaderTable>& ElfParser::get_section_header_table() const {
        return m_section_header_table;
    }

    // ElfParser Implementation
    ElfParser::ElfParser(uintptr_t base_address) : m_base_address(base_address) {}

    uintptr_t ElfParser::file_offset_to_memory_addr(uint64_t offset) {
        if (m_load_bias != 0) {
             // If we have a load bias, we can assume the segments are loaded in memory
             // and we can calculate the address from the program headers.
            for (const auto& phdr : *m_program_header_table) {
                if (phdr.get_type() == PT_LOAD) {
                    if (offset >= phdr.get_offset() && offset < phdr.get_offset() + phdr.get_file_size()) {
                        return m_load_bias + phdr.get_virtual_address() + (offset - phdr.get_offset());
                    }
                }
            }
            return 0;
        }
        // if there is no load bias, the file is likely mapped directly.
        return m_base_address + offset;
    }

    uintptr_t ElfParser::get_load_bias() const {
        return m_load_bias;
    }

    uintptr_t ElfParser::get_plt_rel_location() const {
        return m_plt_rel_location;
    }

    size_t ElfParser::get_plt_rel_size() const {
        return m_plt_rel_size;
    }

    int ElfParser::get_plt_rel_entry_type() const {
        return m_plt_rel_entry_type;
    }

    const Elf64_Sym* ElfParser::get_dynamic_symbol_table() const {
        return m_dynsym;
    }

    const char* ElfParser::get_dynamic_string_table() const {
        return m_dynstr;
    }

    bool ElfParser::parse() {
        m_header = std::make_unique<ElfHeader>(m_base_address);
        if (!m_header->is_valid()) {
            return false;
        }

        m_program_header_table = std::make_unique<ProgramHeaderTable>(m_base_address, *m_header);

        const ProgramHeader* min_vaddr_pt_load = nullptr;
        for (const auto& phdr : *m_program_header_table) {
            if (phdr.get_type() == PT_LOAD) {
                if (min_vaddr_pt_load == nullptr || phdr.get_virtual_address() < min_vaddr_pt_load->get_virtual_address()) {
                    min_vaddr_pt_load = &phdr;
                }
            }
        }

        if (min_vaddr_pt_load == nullptr) {
            // This is unlikely for any valid ELF file.
            return false;
        }
        m_load_bias = m_base_address - min_vaddr_pt_load->get_virtual_address();

        // Dynamic symbols are essential for many operations.
        const auto* pt_dynamic = m_program_header_table->find_first_by_type(PT_DYNAMIC);
        if (pt_dynamic != nullptr) {
            const auto* dyn = reinterpret_cast<const Elf64_Dyn*>(m_load_bias + pt_dynamic->get_virtual_address());
            for (const auto* d = dyn; d->d_tag != DT_NULL; ++d) {
                switch (d->d_tag) {
                    case DT_STRTAB:
                        m_dynstr = reinterpret_cast<const char*>(m_load_bias + d->d_un.d_ptr);
                        break;
                    case DT_SYMTAB:
                        m_dynsym = reinterpret_cast<const Elf64_Sym*>(m_load_bias + d->d_un.d_ptr);
                        break;
                    case DT_GNU_HASH:
                        m_gnu_hash_table = reinterpret_cast<const uint32_t*>(m_load_bias + d->d_un.d_ptr);
                        break;
                    case DT_HASH:
                        m_hash_table = reinterpret_cast<const uint32_t*>(m_load_bias + d->d_un.d_ptr);
                        break;
                    case DT_JMPREL:
                        m_plt_rel_location = m_load_bias + d->d_un.d_ptr;
                        break;
                    case DT_PLTRELSZ:
                        m_plt_rel_size = d->d_un.d_val;
                        break;
                    case DT_PLTREL:
                        m_plt_rel_entry_type = d->d_un.d_val;
                        break;
                }
            }
        }

        // Section headers are optional. They might not be loaded in memory.
        uintptr_t sh_table_addr = file_offset_to_memory_addr(m_header->get_section_header_offset());
        if (sh_table_addr != 0) {
            m_section_header_table = std::make_unique<SectionHeaderTable>(sh_table_addr, *m_header, this);

            // Full symbol table is also optional.
            const auto* symtab_sh = m_section_header_table->get_section_by_name(".symtab");
            if (symtab_sh != nullptr) {
                m_symtab = reinterpret_cast<const Elf64_Sym*>(file_offset_to_memory_addr(symtab_sh->get_offset()));
                m_symtab_count = symtab_sh->get_size() / sizeof(Elf64_Sym);
                const auto* strtab_sh = m_section_header_table->get_section_by_name(".strtab");
                if (strtab_sh != nullptr) {
                    m_strtab = reinterpret_cast<const char*>(file_offset_to_memory_addr(strtab_sh->get_offset()));
                }
            }
        }

        // As long as we have a valid header and program headers, consider parsing successful.
        return true;
    }
    
    static uint32_t gnu_hash(const char *s) {
        uint32_t h = 5381;
        for (unsigned char c = *s; c != '\0'; c = *++s) {
            h = (h << 5) + h + c;
        }
        return h;
    }

    uintptr_t ElfParser::find_symbol(const std::string& symbol_name) {
        if (m_gnu_hash_table != nullptr) {
            auto addr = find_symbol_by_gnu_hash(symbol_name);
            if (addr != 0) return addr;
        }
        if (m_hash_table != nullptr) {
            auto addr = find_symbol_by_hash(symbol_name);
            if (addr != 0) return addr;
        }
        if (m_symtab != nullptr && m_strtab != nullptr) {
            for (size_t i = 0; i < m_symtab_count; ++i) {
                const Elf64_Sym* sym = &m_symtab[i];
                if (strcmp(symbol_name.c_str(), m_strtab + sym->st_name) == 0) {
                     auto type = ELF64_ST_TYPE(sym->st_info);
                    if ((type == STT_FUNC || type == STT_OBJECT || type == STT_GNU_IFUNC) && sym->st_shndx != SHN_UNDEF) {
                        if (type == STT_GNU_IFUNC) {
                            using resolver_t = void* (*)();
                            auto resolver = (resolver_t)(m_load_bias + sym->st_value);
                            return (uintptr_t)resolver();
                        }
                        return m_load_bias + sym->st_value;
                    }
                }
            }
        }
        return 0;
    }

    uintptr_t ElfParser::find_symbol_by_gnu_hash(const std::string& symbol_name) {
        const uint32_t nbuckets = m_gnu_hash_table[0];
        const uint32_t symoffset = m_gnu_hash_table[1];
        const uint32_t bloom_size = m_gnu_hash_table[2];
        const uint32_t bloom_shift = m_gnu_hash_table[3];
        const auto bloom_filter = reinterpret_cast<const Elf64_Addr*>(&m_gnu_hash_table[4]);
        const auto buckets = reinterpret_cast<const uint32_t*>(&bloom_filter[bloom_size]);
        const auto chain = &buckets[nbuckets];

        uint32_t hash = gnu_hash(symbol_name.c_str());

        uint64_t bloom_word = bloom_filter[(hash / 64) % bloom_size];
        uint64_t h1 = hash % 64;
        uint64_t h2 = (hash >> bloom_shift) % 64;
        if (!((bloom_word >> h1) & (bloom_word >> h2) & 1)) {
            return 0;
        }
        
        uint32_t sym_idx = buckets[hash % nbuckets];
        if (sym_idx < symoffset) {
            return 0;
        }

        const Elf64_Sym* sym = &m_dynsym[sym_idx];
        const uint32_t* hash_chain = &chain[sym_idx - symoffset];

        for (;; sym_idx++, sym++, hash_chain++) {
            uint32_t chain_hash = *hash_chain;
            if ((hash | 1) == (chain_hash | 1)) {
                if (strcmp(symbol_name.c_str(), m_dynstr + sym->st_name) == 0) {
                    auto type = ELF64_ST_TYPE(sym->st_info);
                    if ((type == STT_FUNC || type == STT_OBJECT || type == STT_GNU_IFUNC) && sym->st_shndx != SHN_UNDEF) {
                        if (type == STT_GNU_IFUNC) {
                            using resolver_t = void* (*)();
                            auto resolver = (resolver_t)(m_load_bias + sym->st_value);
                            return (uintptr_t)resolver();
                        }
                        return m_load_bias + sym->st_value;
                    }
                }
            }
            if (chain_hash & 1) {
                break;
            }
        }

        return 0;
    }
    
    static uint32_t elf_hash(const char *s) {
        uint32_t h = 0, g;
        while (*s) {
            h = (h << 4) + *s++;
            if ((g = h & 0xf0000000)) {
                h ^= g >> 24;
            }
            h &= ~g;
        }
        return h;
    }

    uintptr_t ElfParser::find_symbol_by_hash(const std::string& symbol_name) {
        const uint32_t nbucket = m_hash_table[0];
        const uint32_t nchain = m_hash_table[1];
        const auto bucket = &m_hash_table[2];
        const auto chain = &bucket[nbucket];

        uint32_t hash = elf_hash(symbol_name.c_str());
        for (uint32_t i = bucket[hash % nbucket]; i != 0; i = chain[i]) {
            const Elf64_Sym* sym = &m_dynsym[i];
            if (strcmp(symbol_name.c_str(), m_dynstr + sym->st_name) == 0) {
                auto type = ELF64_ST_TYPE(sym->st_info);
                if ((type == STT_FUNC || type == STT_OBJECT || type == STT_GNU_IFUNC) && sym->st_shndx != SHN_UNDEF) {
                    if (type == STT_GNU_IFUNC) {
                        using resolver_t = void* (*)();
                        auto resolver = (resolver_t)(m_load_bias + sym->st_value);
                        return (uintptr_t)resolver();
                    }
                    return m_load_bias + sym->st_value;
                }
            }
        }
        return 0;
    }
}