#pragma once
#include <cstdint>
#include <string>
#include <vector>

std::vector<uint8_t> readPeVersionResource(const std::string& path);
struct VersionNode {
    uint32_t offset, length, valueOffset, valueBytes, valueLength, childrenOffset;
    uint16_t type;
    std::string key;
};
VersionNode readVersionNode(const uint8_t* data, uint32_t bytes, uint32_t offset);
bool findVersionNode(const uint8_t* data, uint32_t bytes, const std::string& path, VersionNode& result);
