#pragma once
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace wire_fixtures {
struct Fixture {
    std::vector<uint8_t> bytes;
    bool response = false;
};
inline std::map<std::string, Fixture> read(const std::filesystem::path& path) {
    // Fixture JSON keeps names and field hex on separate lines for this test-only reader.
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open wire fixtures: " + path.string());
    std::map<std::string, Fixture> fixtures;
    Fixture* current = nullptr;
    std::string line;
    while (std::getline(file, line)) {
        const std::string name_prefix = "      \"name\": \"";
        if (line.compare(0, name_prefix.size(), name_prefix) == 0) {
            const auto end = line.find('"', name_prefix.size());
            const auto inserted = fixtures.emplace(line.substr(name_prefix.size(), end - name_prefix.size()), Fixture{});
            if (!inserted.second) throw std::runtime_error("Duplicate wire fixture");
            current = &inserted.first->second;
        }
        if (!current) continue;
        if (line == "      \"response\": true,") current->response = true;
        const auto hex = line.find("\"hex\": \"");
        if (hex == std::string::npos) continue;
        const auto start = hex + 8, end = line.find('"', start);
        if (end == std::string::npos || (end - start) % 2) throw std::runtime_error("Invalid fixture hex");
        for (auto pos = start; pos < end; pos += 2) {
            unsigned byte = 0;
            const auto parsed = std::from_chars(line.data() + pos, line.data() + pos + 2, byte, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != line.data() + pos + 2) throw std::runtime_error("Invalid fixture byte");
            current->bytes.push_back(static_cast<uint8_t>(byte));
        }
    }
    if (!file.eof() || fixtures.empty()) throw std::runtime_error("Cannot read wire fixtures");
    return fixtures;
}
}
