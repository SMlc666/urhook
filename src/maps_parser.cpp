#include "ur/maps_parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>

namespace ur::maps_parser {

    MapInfo::MapInfo(std::uintptr_t start, std::uintptr_t end, std::string perms, std::size_t offset, std::string path)
        : m_start(start), m_end(end), m_perms(std::move(perms)), m_offset(offset), m_path(std::move(path)) {}

    elf_parser::ElfParser* MapInfo::get_elf_parser() const {
        if (!m_elf_parser) {
            m_elf_parser = std::make_unique<elf_parser::ElfParser>(m_start);
            if (!m_elf_parser->parse()) {
                m_elf_parser.reset(); // Reset if parsing fails
            }
        }
        return m_elf_parser.get();
    }

    std::uintptr_t MapInfo::get_start() const {
        return m_start;
    }

    std::uintptr_t MapInfo::get_end() const {
        return m_end;
    }

    const std::string& MapInfo::get_perms() const {
        return m_perms;
    }

    const std::string& MapInfo::get_path() const {
        return m_path;
    }

    std::vector<MapInfo> MapsParser::parse() {
        std::vector<MapInfo> maps;
        std::ifstream file("/proc/self/maps");
        std::string line;

        while (std::getline(file, line)) {
            std::stringstream ss(line);
            uintptr_t start, end, offset;
            std::string perms, dev, path;
            ino_t inode;
            char dash;

            ss >> std::hex >> start >> dash >> end >> perms >> offset >> dev >> inode;
            
            // After the inode, there might be whitespace before the path
            ss >> std::ws;
            std::getline(ss, path);

            maps.emplace_back(start, end, perms, offset, path);
        }
        return maps;
    }

    const MapInfo* MapsParser::find_map_by_path(const std::vector<MapInfo>& maps, const std::string& path) {
        for (const auto& info : maps) {
            if (info.get_path() == path) {
                return &info;
            }
        }
        return nullptr;
    }

    const MapInfo* MapsParser::find_map_by_addr(const std::vector<MapInfo>& maps, std::uintptr_t addr) {
        auto it = std::find_if(maps.begin(), maps.end(), [&](const MapInfo& info) {
            return addr >= info.get_start() && addr < info.get_end();
        });
        return it != maps.end() ? &(*it) : nullptr;
    }

}

