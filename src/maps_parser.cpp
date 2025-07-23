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
        std::map<std::string, MapInfo*> path_map;
        std::ifstream file("/proc/self/maps");
        std::string line;

        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::uintptr_t start, end;
            std::string perms, path;
            std::size_t offset;
            char dash;

            ss >> std::hex >> start >> dash >> end >> perms;
            ss.seekg(35); // Move to offset position
            ss >> std::hex >> offset;

            ss >> std::ws;
            std::getline(ss, path);

            if (path.empty() || path[0] == '[') {
                maps.emplace_back(start, end, perms, offset, path);
                continue;
            }

            auto it = path_map.find(path);
            if (it == path_map.end()) {
                maps.emplace_back(start, end, perms, offset, path);
                path_map[path] = &maps.back();
            } else {
                // Extend the existing map region and merge permissions
                it->second->m_end = end;
                if (perms.find('r') != std::string::npos && it->second->m_perms.find('r') == std::string::npos) it->second->m_perms += 'r';
                if (perms.find('w') != std::string::npos && it->second->m_perms.find('w') == std::string::npos) it->second->m_perms += 'w';
                if (perms.find('x') != std::string::npos && it->second->m_perms.find('x') == std::string::npos) it->second->m_perms += 'x';
            }
        }
        return maps;
    }

    const MapInfo* MapsParser::find_map_by_path(const std::vector<MapInfo>& maps, const std::string& path) {
        auto it = std::find_if(maps.begin(), maps.end(), [&](const MapInfo& info) {
            if (info.get_path() == path) {
                return true;
            }
            // Also check if the path ends with the given path, e.g. /apex/.../libc.so for libc.so
            if (!info.get_path().empty() && info.get_path().length() > path.length()) {
                if (info.get_path().substr(info.get_path().length() - path.length()) == path) {
                    // Check if the character before the match is a '/'
                    if (info.get_path()[info.get_path().length() - path.length() - 1] == '/') {
                        return true;
                    }
                }
            }
            return false;
        });
        return it != maps.end() ? &(*it) : nullptr;
    }

    const MapInfo* MapsParser::find_map_by_addr(const std::vector<MapInfo>& maps, std::uintptr_t addr) {
        auto it = std::find_if(maps.begin(), maps.end(), [&](const MapInfo& info) {
            return addr >= info.get_start() && addr < info.get_end();
        });
        return it != maps.end() ? &(*it) : nullptr;
    }

}

