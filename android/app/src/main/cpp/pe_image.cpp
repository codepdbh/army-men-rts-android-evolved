#include "pe_image.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace {
constexpr uint64_t kGuestSize = uint64_t{1} << 32;
constexpr uint32_t kExpectedBase = 0x00400000;
constexpr uint32_t kExpectedImageSize = 0x0037c000;
constexpr uint32_t kExpectedEntry = 0x0051ea26;
constexpr uint32_t kImportBase = 0x70000000;

uint16_t read16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t read32(const uint8_t* p) { return uint32_t(read16(p)) | uint32_t(read16(p + 2)) << 16; }

void requireFile(const std::vector<uint8_t>& file, uint64_t offset, uint64_t size) {
    if (offset > file.size() || size > file.size() - offset)
        throw std::runtime_error("PE truncado o desplazamiento fuera de rango");
}
void requireImage(uint32_t imageSize, uint64_t rva, uint64_t size) {
    if (rva > imageSize || size > imageSize - rva)
        throw std::runtime_error("Seccion o importacion fuera de la imagen PE");
}
}

void* PeImage::address(uint32_t va, uint64_t bytes) const {
    return memory_.address(va, bytes);
}

void PeImage::commit(uint32_t va, uint64_t bytes) {
    memory_.commit(va, bytes);
}

PeImageInfo PeImage::load(const std::string& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("No se pudo abrir AMRTS.EXE");
    auto length = stream.tellg();
    if (length < 0 || length > 64 * 1024 * 1024) throw std::runtime_error("Invalid PE file size");
    std::vector<uint8_t> file(static_cast<size_t>(length));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(file.data()), file.size())) throw std::runtime_error("PE file read failed");
    requireFile(file, 0, 64);
    if (read16(file.data()) != 0x5a4d) throw std::runtime_error("No es un ejecutable MZ");
    uint32_t nt = read32(file.data() + 0x3c);
    requireFile(file, nt, 24);
    if (read32(file.data() + nt) != 0x4550 || read16(file.data() + nt + 4) != 0x14c)
        throw std::runtime_error("Se requiere un PE32 x86");
    uint16_t sectionCount = read16(file.data() + nt + 6);
    uint16_t optionalSize = read16(file.data() + nt + 20);
    if (!sectionCount || sectionCount > 96 || optionalSize < 224)
        throw std::runtime_error("Cabecera PE opcional invalida");
    requireFile(file, uint64_t(nt) + 24, optionalSize);
    const uint8_t* opt = file.data() + nt + 24;
    if (read16(opt) != 0x10b) throw std::runtime_error("Se requiere PE32, no PE32+");
    PeImageInfo info;
    info.sourcePath = path;
    info.imageBase = read32(opt + 28);
    info.imageSize = read32(opt + 56);
    uint32_t entryRva = read32(opt + 16);
    uint32_t headersSize = read32(opt + 60);
    uint32_t directoryCount = read32(opt + 92);
    if (info.imageBase != kExpectedBase || info.imageSize != kExpectedImageSize ||
        entryRva != kExpectedEntry - kExpectedBase)
        throw std::runtime_error("Esta version de AMRTS.EXE no coincide con el codigo traducido");
    requireImage(info.imageSize, 0, headersSize);
    requireFile(file, 0, headersSize);
    uint64_t sectionTable = uint64_t(nt) + 24 + optionalSize;
    requireFile(file, sectionTable, uint64_t(sectionCount) * 40);
    commit(info.imageBase, info.imageSize);
    std::memcpy(address(info.imageBase, headersSize), file.data(), headersSize);
    for (uint32_t i = 0; i < sectionCount; ++i) {
        const uint8_t* sec = file.data() + sectionTable + uint64_t(i) * 40;
        uint32_t virtualSize = read32(sec + 8);
        uint32_t rva = read32(sec + 12);
        uint32_t rawSize = read32(sec + 16);
        uint32_t rawOffset = read32(sec + 20);
        requireImage(info.imageSize, rva, std::max(virtualSize, rawSize));
        if (rawSize) {
            requireFile(file, rawOffset, rawSize);
            std::memcpy(address(info.imageBase + rva, rawSize), file.data() + rawOffset, rawSize);
        }
    }
    info.entry = info.imageBase + entryRva;
    info.sections = sectionCount;
    if (directoryCount > 1) {
        uint32_t importRva = read32(opt + 104);
        uint32_t importSize = read32(opt + 108);
        if (importSize) {
            requireImage(info.imageSize, importRva, importSize);
            bool ended = false;
            for (uint64_t offset = 0; offset + 20 <= importSize; offset += 20) {
                const auto* descriptor = static_cast<const uint8_t*>(address(info.imageBase + importRva + offset, 20));
                uint32_t originalThunk = read32(descriptor);
                uint32_t nameRva = read32(descriptor + 12);
                uint32_t firstThunk = read32(descriptor + 16);
                if (!originalThunk && !nameRva && !firstThunk) { ended = true; break; }
                requireImage(info.imageSize, nameRva, 1);
                const auto* name = static_cast<const char*>(address(info.imageBase + nameRva, 1));
                size_t limit = info.imageSize - nameRva;
                const void* end = std::memchr(name, 0, limit);
                if (!end || static_cast<const char*>(end) - name > 128)
                    throw std::runtime_error("Nombre de DLL importada invalido");
                info.importedLibraries.emplace_back(name);
                uint32_t thunkRva = originalThunk ? originalThunk : firstThunk;
                std::vector<PeImageInfo::Import> descriptorImports;
                for (uint32_t j = 0; ; ++j) {
                    uint64_t slot = uint64_t(thunkRva) + uint64_t(j) * 4;
                    requireImage(info.imageSize, slot, 4);
                    requireImage(info.imageSize, uint64_t(firstThunk) + uint64_t(j) * 4, 4);
                    uint32_t thunk = read32(static_cast<const uint8_t*>(address(info.imageBase + slot, 4)));
                    if (!thunk) break;
                    std::string symbol;
                    if (thunk & 0x80000000u) {
                        symbol = "#" + std::to_string(thunk & 0xffff);
                    } else {
                        requireImage(info.imageSize, thunk, 3);
                        const char* text = static_cast<const char*>(address(info.imageBase + thunk + 2, 1));
                        const void* terminator = std::memchr(text, 0, info.imageSize - thunk - 2);
                        if (!terminator || static_cast<const char*>(terminator) - text > 256)
                            throw std::runtime_error("Nombre de funcion importada invalido");
                        symbol = text;
                    }
                    if (++info.imports > 4096) throw std::runtime_error("Demasiadas importaciones PE");
                    uint32_t target = kImportBase + (info.imports - 1) * 16;
                    descriptorImports.push_back({info.imageBase + firstThunk + j * 4,
                                                 target, name, std::move(symbol)});
                }
                // OriginalFirstThunk may be absent, in which case the name
                // thunks are the IAT itself. Patch only after collecting them.
                for (auto& symbol : descriptorImports) {
                    auto* slot = static_cast<uint8_t*>(address(symbol.iatSlot, 4));
                    slot[0] = uint8_t(symbol.target);
                    slot[1] = uint8_t(symbol.target >> 8);
                    slot[2] = uint8_t(symbol.target >> 16);
                    slot[3] = uint8_t(symbol.target >> 24);
                    info.importTable.push_back(std::move(symbol));
                }
            }
            if (!ended) throw std::runtime_error("Directorio de importaciones sin terminador");
        }
    }
    return info;
}
