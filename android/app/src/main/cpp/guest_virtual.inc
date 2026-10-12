struct VirtualReservation { uint32_t bytes, protection; };
std::map<uint32_t, VirtualReservation> virtualReservations;
void resetVirtual() { virtualReservations.clear(); }

bool virtualDispatch(const std::string& name) {
    if (name == "IsBadReadPtr" || name == "IsBadWritePtr" || name == "IsBadCodePtr") {
        uint32_t bytes = name == "IsBadCodePtr" ? 1 : arg(1);
        bool valid = name == "IsBadWritePtr" ? activeImage->memory().writable(arg(0), bytes) :
                                               activeImage->memory().readable(arg(0), bytes);
        ret(!valid, name == "IsBadCodePtr" ? 1 : 2); return true;
    }
    if (name == "VirtualQuery") {
        uint32_t address = arg(0), output = arg(1);
        if (address >= 0x80000000u || arg(2) < 28) { kernelError(87); ret(0, 3); return true; }
        auto region = activeImage->memory().query(address);
        uint64_t end = std::min(region.base + region.bytes, uint64_t(0x80000000));
        uint32_t allocationBase = 0, allocationProtection = 0, type = 0;
        auto after = virtualReservations.upper_bound(address);
        if (after != virtualReservations.begin()) {
            auto reservation = std::prev(after);
            uint64_t limit = uint64_t(reservation->first) + reservation->second.bytes;
            if (address < limit) {
                end = std::min(end, limit); allocationBase = reservation->first;
                allocationProtection = reservation->second.protection; type = 0x20000;
                if (region.state == 0x10000) region.state = 0x2000;
            }
        }
        if (!allocationBase && region.state == 0x1000) {
            uint64_t imageEnd = uint64_t(activeInfo->imageBase) + activeInfo->imageSize;
            if (address >= activeInfo->imageBase && address < imageEnd) {
                allocationBase = activeInfo->imageBase; type = 0x1000000; end = std::min(end, imageEnd);
            } else {
                allocationBase = uint32_t(region.base); type = 0x20000;
                if (region.base < activeInfo->imageBase) end = std::min(end, uint64_t(activeInfo->imageBase));
            }
            allocationProtection = region.protection;
        }
        if (!allocationBase && after != virtualReservations.end()) end = std::min(end, uint64_t(after->first));
        queryMappedMemory(address,allocationBase,allocationProtection,end,type);
        uint32_t fields[] = {uint32_t(region.base), allocationBase, allocationProtection,
            uint32_t(end - region.base), region.state, region.protection, type};
        std::memcpy(kernelPtr(output, 28, true), fields, 28); ret(28, 3); return true;
    }
    if (name == "VirtualAlloc") {
        uint32_t requested = arg(0), bytes = arg(1), flags = arg(2), protection = arg(3);
        if (!bytes || !(flags & 0x3000) || (flags & ~0x3000u)) { kernelError(87); ret(0, 4); return true; }
        if (protection != 1 && protection != 2 && protection != 4 && protection != 8 &&
            protection != 0x10 && protection != 0x20 && protection != 0x40 && protection != 0x80) {
            kernelError(87); ret(0, 4); return true;
        }
        uint32_t page = uint32_t(sysconf(_SC_PAGESIZE));
        uint64_t start = requested ? uint64_t(requested) / page * page : 0;
        uint64_t end = requested ? (uint64_t(requested) + bytes + page - 1) / page * page :
                                  (uint64_t(bytes) + page - 1) / page * page;
        if ((flags & 0x2000) || !requested) {
            if (requested) start = uint64_t(requested) / 65536 * 65536;
            uint64_t needed = end - start;
            if (!requested) {
                start = 0x40000000;
                for (const auto& allocation : virtualReservations) {
                    if (uint64_t(allocation.first) >= start + needed) break;
                    start = std::max(start, uint64_t(allocation.first) + allocation.second.bytes);
                    start = (start + 65535) / 65536 * 65536;
                }
                end = start + needed;
            }
            bool conflict = start < 0x40000000 || end > 0x60000000;
            for (const auto& allocation : virtualReservations)
                conflict |= start < uint64_t(allocation.first) + allocation.second.bytes && allocation.first < end;
            if (conflict) { kernelError(487); ret(0, 4); return true; }
            virtualReservations.emplace(uint32_t(start), VirtualReservation{uint32_t(end - start), protection});
        } else {
            auto after = virtualReservations.upper_bound(uint32_t(start));
            if (after == virtualReservations.begin()) { kernelError(487); ret(0, 4); return true; }
            auto allocation = std::prev(after);
            if (end > uint64_t(allocation->first) + allocation->second.bytes) { kernelError(487); ret(0, 4); return true; }
        }
        if (flags & 0x1000) {
            activeImage->commit(uint32_t(start), end - start);
            uint32_t previous = 0;
            if (!activeImage->memory().protect(uint32_t(start), end - start, protection, previous))
                throw GuestStop("VirtualAlloc protection failed");
        }
        ret(uint32_t(start), 4); return true;
    }
    if (name == "VirtualFree") {
        uint32_t address = arg(0), bytes = arg(1), flags = arg(2);
        auto after = virtualReservations.upper_bound(address);
        if (after == virtualReservations.begin()) { kernelError(487); ret(0, 3); return true; }
        auto allocation = std::prev(after);
        uint64_t limit = uint64_t(allocation->first) + allocation->second.bytes;
        if (flags == 0x8000 && address == allocation->first && !bytes) {
            bool ok = activeImage->memory().decommit(address, allocation->second.bytes);
            if (ok) virtualReservations.erase(allocation);
            ret(ok, 3);
        } else if (flags == 0x4000 && address < limit && (!bytes || uint64_t(address) + bytes <= limit)) {
            ret(activeImage->memory().decommit(address, bytes ? bytes : limit - address), 3);
        } else { kernelError(87); ret(0, 3); }
        return true;
    }
    if (name == "VirtualProtect") {
        uint32_t previous = 0;
        kernelPtr(arg(3), 4, true);
        if (!activeImage->memory().protect(arg(0), arg(1), arg(2), previous)) { kernelError(87); ret(0, 4); }
        else { write32(arg(3), previous); ret(1, 4); }
        return true;
    }
    return false;
}
