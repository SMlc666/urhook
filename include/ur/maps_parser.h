#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "elf_parser.h"

namespace ur::maps_parser {

    class MapInfo {
    public:
        MapInfo(std::uintptr_t start, std::uintptr_t end, std::string perms, std::size_t offset, std::string path);

        elf_parser::ElfParser* get_elf_parser() const;

        [[nodiscard]] std::uintptr_t get_start() const;
        [[nodiscard]] std::uintptr_t get_end() const;
        [[nodiscard]] const std::string& get_perms() const;
        [[nodiscard]] const std::string& get_path() const;

    private:
        friend class MapsParser;
        std::uintptr_t m_start;
        std::uintptr_t m_end;
        std::string m_perms;
        std::size_t m_offset;
        std::string m_path;
        mutable std::unique_ptr<elf_parser::ElfParser> m_elf_parser;
    };

    class MapsParser {
    public:
        static std::vector<MapInfo> parse();
        static const MapInfo* find_map_by_path(const std::vector<MapInfo>& maps, const std::string& path);
        static const MapInfo* find_map_by_addr(const std::vector<MapInfo>& maps, std::uintptr_t addr);
    };

}
