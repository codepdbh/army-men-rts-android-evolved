struct VersionBuffer { uint32_t blobBytes, totalBytes, tail; std::map<std::string,uint32_t> strings; };
std::map<uint32_t,VersionBuffer> versionBuffers;
void resetVersions() { versionBuffers.clear(); }
bool versionDispatch(const std::string& name) {
    if (name == "GetFileVersionInfoSizeA" || name == "GetFileVersionInfoA") {
        auto path = gamePath(guestString(arg(0)));
        auto blob = readPeVersionResource(path.string());
        if (blob.empty()) { kernelError(std::filesystem::exists(path) ? 1812 : 2);
            ret(0, name == "GetFileVersionInfoSizeA" ? 2 : 4); return true; }
        uint32_t needed = uint32_t(blob.size()) * 2 + 256;
        if (name == "GetFileVersionInfoSizeA") {
            if (arg(1)) write32(arg(1), 0); ret(needed, 2);
        } else if (arg(2) < needed) { kernelError(122); ret(0, 4); }
        else {
            std::memset(kernelPtr(arg(3), needed, true), 0, needed);
            std::memcpy(kernelPtr(arg(3), blob.size(), true), blob.data(), blob.size());
            versionBuffers[arg(3)] = {uint32_t(blob.size()), needed, (uint32_t(blob.size()) + 3) & ~3u, {}};
            ret(1, 4);
        }
        return true;
    }
    if (name == "VerQueryValueA") {
        uint32_t block = arg(0); kernelPtr(block, 6);
        auto record = versionBuffers.find(block);
        uint32_t bytes = record == versionBuffers.end() ? MEM16(block) : record->second.blobBytes;
        const auto* blob = static_cast<const uint8_t*>(kernelPtr(block, bytes));
        std::string path = guestString(arg(1)); VersionNode node{};
        if (!findVersionNode(blob, bytes, path, node)) {
            write32(arg(2), 0); write32(arg(3), 0); kernelError(1813); ret(0, 4); return true;
        }
        uint32_t value = block + node.valueOffset, length = node.valueLength;
        if (node.type == 1) {
            if (record == versionBuffers.end()) throw GuestStop("ANSI version query needs GetFileVersionInfoA buffer");
            auto cached = record->second.strings.find(lower(path));
            if (cached != record->second.strings.end()) value = cached->second;
            else {
                if (node.valueLength + record->second.tail > record->second.totalBytes) { kernelError(122); ret(0, 4); return true; }
                value = block + record->second.tail;
                for (uint32_t i = 0; i < node.valueLength; ++i) {
                    int c = encodeByte(1252, MEM16(block + node.valueOffset + i * 2)); MEM8(value + i) = c < 0 ? '?' : uint8_t(c);
                }
                record->second.tail += node.valueLength;
                record->second.strings.emplace(lower(path), value);
            }
        }
        write32(arg(2), value); write32(arg(3), length); ret(1, 4); return true;
    }
    return false;
}
