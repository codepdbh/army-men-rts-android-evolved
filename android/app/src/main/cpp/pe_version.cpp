#include "pe_version.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {
uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t* p) { return uint32_t(u16(p)) | uint32_t(u16(p + 2)) << 16; }
void range(uint64_t total, uint64_t start, uint64_t bytes) {
    if (start > total || bytes > total - start) throw std::runtime_error("Truncated PE version resource");
}
uint32_t align4(uint32_t value) { return (value + 3) & ~3u; }
std::string lowerKey(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}
}

std::vector<uint8_t> readPeVersionResource(const std::string& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return {};
    auto length = stream.tellg();
    if (length < 0 || length > 64 * 1024 * 1024) throw std::runtime_error("Invalid version PE file size");
    std::vector<uint8_t> file(static_cast<size_t>(length));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(file.data()), file.size())) throw std::runtime_error("Version PE file read failed");
    if (file.size() < 64 || u16(file.data()) != 0x5a4d) return {};
    uint32_t nt = u32(file.data() + 60); range(file.size(), nt, 24);
    if (u32(file.data() + nt) != 0x4550) return {};
    uint32_t count = u16(file.data() + nt + 6), optionalBytes = u16(file.data() + nt + 20);
    if (!count || count > 96 || optionalBytes < 120) return {};
    range(file.size(), uint64_t(nt) + 24, optionalBytes);
    const uint8_t* opt = file.data() + nt + 24;
    uint32_t directory = u16(opt) == 0x10b ? 112 : u16(opt) == 0x20b ? 128 : 0;
    if (!directory || directory + 8 > optionalBytes) return {};
    uint32_t resourceRva = u32(opt + directory), resourceSize = u32(opt + directory + 4);
    if (!resourceRva || resourceSize < 16) return {};
    uint64_t sections = uint64_t(nt) + 24 + optionalBytes;
    range(file.size(), sections, uint64_t(count) * 40);
    auto offset = [&](uint32_t rva, uint32_t bytes) -> uint64_t {
        if (rva < u32(opt + 60)) { range(file.size(), rva, bytes); return rva; }
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* section = file.data() + sections + i * 40;
            uint32_t start = u32(section + 12), rawBytes = u32(section + 16);
            if (rva >= start && uint64_t(rva) + bytes <= uint64_t(start) + rawBytes) {
                uint64_t result = uint64_t(u32(section + 20)) + rva - start;
                range(file.size(), result, bytes); return result;
            }
        }
        throw std::runtime_error("Version resource outside PE sections");
    };
    uint64_t root = offset(resourceRva, resourceSize);
    auto entry = [&](uint32_t directoryOffset, int requested) -> uint32_t {
        range(resourceSize, directoryOffset, 16);
        const uint8_t* header = file.data() + root + directoryOffset;
        uint32_t entries = uint32_t(u16(header + 12)) + u16(header + 14);
        range(resourceSize, uint64_t(directoryOffset) + 16, uint64_t(entries) * 8);
        for (uint32_t i = 0; i < entries; ++i) {
            const uint8_t* candidate = header + 16 + i * 8;
            uint32_t id = u32(candidate);
            if (!(id & 0x80000000u) && (requested < 0 || id == uint32_t(requested))) return u32(candidate + 4);
        }
        return UINT32_MAX;
    };
    uint32_t current = entry(0, 16);
    if (current == UINT32_MAX || !(current & 0x80000000u)) return {};
    current = entry(current & 0x7fffffffu, -1);
    if (current == UINT32_MAX || !(current & 0x80000000u)) return {};
    current = entry(current & 0x7fffffffu, -1);
    if (current == UINT32_MAX || (current & 0x80000000u)) return {};
    range(resourceSize, current, 16);
    const uint8_t* dataEntry = file.data() + root + current;
    uint32_t bytes = u32(dataEntry + 4);
    uint64_t data = offset(u32(dataEntry), bytes);
    if (!bytes || bytes > 65535) throw std::runtime_error("Invalid PE version resource size");
    return {file.begin() + data, file.begin() + data + bytes};
}

VersionNode readVersionNode(const uint8_t* data, uint32_t bytes, uint32_t offset) {
    range(bytes, offset, 6);
    VersionNode result{};
    result.offset = offset; result.length = u16(data + offset);
    result.valueLength = u16(data + offset + 2); result.type = u16(data + offset + 4);
    if (result.length < 6) throw std::runtime_error("Invalid version node length");
    range(bytes, offset, result.length);
    uint32_t cursor = offset + 6, end = offset + result.length;
    for (;;) {
        range(end, cursor, 2);
        uint16_t c = u16(data + cursor); cursor += 2;
        if (!c) break;
        if (c > 127 || result.key.size() >= 256) throw std::runtime_error("Invalid version node key");
        result.key.push_back(char(c));
    }
    result.valueOffset = align4(cursor);
    result.valueBytes = result.type == 1 ? result.valueLength * 2 : result.valueLength;
    if (result.valueBytes) range(end, result.valueOffset, result.valueBytes);
    result.childrenOffset = align4(result.valueOffset + result.valueBytes);
    return result;
}

bool findVersionNode(const uint8_t* data, uint32_t bytes, const std::string& path, VersionNode& result) {
    result = readVersionNode(data, bytes, 0);
    if (result.key != "VS_VERSION_INFO") return false;
    size_t cursor = 0;
    while (cursor < path.size()) {
        while (cursor < path.size() && (path[cursor] == '\\' || path[cursor] == '/')) ++cursor;
        if (cursor == path.size()) break;
        size_t end = path.find_first_of("\\/", cursor);
        if (end == std::string::npos) end = path.size();
        std::string key = lowerKey(path.substr(cursor, end - cursor));
        bool found = false;
        for (uint32_t child = result.childrenOffset; child + 6 <= result.offset + result.length; ) {
            VersionNode node = readVersionNode(data, result.offset + result.length, child);
            if (lowerKey(node.key) == key) { result = node; found = true; break; }
            child = align4(child + node.length);
        }
        if (!found) return false;
        cursor = end;
    }
    return true;
}
