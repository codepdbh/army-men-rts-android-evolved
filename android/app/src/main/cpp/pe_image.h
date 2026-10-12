#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "guest_memory.h"

struct PeImageInfo {
    struct Import {
        uint32_t iatSlot = 0;
        uint32_t target = 0;
        std::string library;
        std::string name;
    };
    uint32_t imageBase = 0;
    uint32_t imageSize = 0;
    uint32_t entry = 0;
    uint32_t sections = 0;
    uint32_t imports = 0;
    std::vector<std::string> importedLibraries;
    std::vector<Import> importTable;
    std::string sourcePath;
};

// A reserved 4 GiB guest address space keeps every x86 virtual address intact.
// The PE is data: ARM64 executes translated functions, never these x86 bytes.
class PeImage {
public:
    PeImage() = default;
    ~PeImage() = default;
    PeImage(const PeImage&) = delete;
    PeImage& operator=(const PeImage&) = delete;
    PeImageInfo load(const std::string& path);
    void commit(uint32_t va, uint64_t bytes);
    void* address(uint32_t va, uint64_t bytes) const;
    void* base() const { return memory_.base(); }
    GuestMemory& memory() { return memory_; }
private:
    GuestMemory memory_;
};
