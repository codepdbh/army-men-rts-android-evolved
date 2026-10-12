#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <map>
#include <algorithm>
#include <vector>
#include <cerrno>

// The guest's virtual address space, independent of host pointer values.
// Reserve only: committed regions are explicitly made accessible on demand.
// A 64-bit host reserves the full 4 GB. A 32-bit host (armeabi-v7a) cannot:
// the Java heap and ART's boot image sit inside any 2 GB window, so it reserves
// 1 GB and GuestHeaps keeps every guest allocation below that.
class GuestMemory {
public:
#if UINTPTR_MAX > 0xffffffffu
    static constexpr uint64_t size = uint64_t{1} << 32;
#else
    static constexpr uint64_t size = uint64_t{1} << 30;
#endif
private:
    void* base_ = MAP_FAILED;
    std::map<uint64_t,uint64_t> committed_;
    std::map<uint64_t,uint32_t> protection_;
    uint64_t page_=static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
    bool accessible(uint32_t va,uint64_t bytes,bool write)const{
        if(!bytes)return true;if(uint64_t(va)>=size||bytes>size-va)return false;
        auto region=committed_.upper_bound(va);if(region==committed_.begin())return false;
        --region;if(uint64_t(va)+bytes>region->second)return false;
        for(uint64_t page=uint64_t(va)/page_*page_;page<uint64_t(va)+bytes;page+=page_){
            auto found=protection_.find(page);uint32_t value=found==protection_.end()?4:found->second;
            if(write?(value!=4&&value!=8&&value!=0x40&&value!=0x80):(value==1||value==0x10))return false;
        }return true;
    }
public:
    GuestMemory() {
        base_ = mmap(nullptr, size_t(size), PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (base_ == MAP_FAILED) throw std::runtime_error("Guest address-space reservation failed");
    }
    GuestMemory(const GuestMemory&) = delete;
    GuestMemory& operator=(const GuestMemory&) = delete;
    ~GuestMemory() { if (base_ != MAP_FAILED) munmap(base_, size_t(size)); }
    void* base() const { return base_; }
    bool readable(uint32_t va,uint64_t bytes)const{
        return accessible(va,bytes,false);
    }
    bool writable(uint32_t va,uint64_t bytes)const{return accessible(va,bytes,true);}
    struct Region { uint64_t base, bytes; uint32_t protection, state; };
    Region query(uint32_t va) const {
        uint64_t start = uint64_t(va) / page_ * page_;
        auto next = committed_.upper_bound(start);
        if (next != committed_.begin()) {
            auto current = std::prev(next);
            if (start < current->second) {
                auto protection = protection_.find(start);
                uint32_t flags = protection == protection_.end() ? 4 : protection->second;
                uint64_t end = start + page_;
                while (end < current->second) {
                    auto found = protection_.find(end);
                    if ((found == protection_.end() ? 4 : found->second) != flags) break;
                    end += page_;
                }
                return {start, end - start, flags, 0x1000};
            }
        }
        uint64_t end = next == committed_.end() ? size : next->first;
        return {start, end - start, 0, 0x10000};
    }
    bool decommit(uint32_t va, uint64_t bytes) {
        if (!bytes || uint64_t(va) >= size || bytes > size - va) return false;
        uint64_t start = uint64_t(va) / page_ * page_;
        uint64_t end = (uint64_t(va) + bytes + page_ - 1) / page_ * page_;
        if (!start || mprotect(static_cast<unsigned char*>(base_) + start, end - start, PROT_NONE)) return false;
        madvise(static_cast<unsigned char*>(base_) + start, end - start, MADV_DONTNEED);
        auto first = committed_.upper_bound(start);
        if (first != committed_.begin()) --first;
        std::vector<std::pair<uint64_t,uint64_t>> keep;
        while (first != committed_.end() && first->first < end) {
            if (first->second <= start) { ++first; continue; }
            if (first->first < start) keep.emplace_back(first->first, start);
            if (first->second > end) keep.emplace_back(end, first->second);
            first = committed_.erase(first);
        }
        for (const auto& part : keep) committed_.emplace(part);
        protection_.erase(protection_.lower_bound(start), protection_.lower_bound(end));
        return true;
    }
    bool mapFile(uint32_t va, uint64_t bytes, int fd, uint64_t offset, bool shared, uint32_t flags) {
        if (!va || !bytes || va % page_ || offset % page_ || bytes > size - va) return false;
        uint64_t rounded = (bytes + page_ - 1) / page_ * page_;
        commit(va, rounded);
        int native = PROT_READ;
        if (flags == 4 || flags == 8 || flags == 0x40 || flags == 0x80) native |= PROT_WRITE;
        void* target = address(va, rounded);
        void* mapped = mmap(target, rounded, native, (shared ? MAP_SHARED : MAP_PRIVATE) | MAP_FIXED, fd, off_t(offset));
        if (mapped == MAP_FAILED) { int error = errno; decommit(va, rounded); errno = error; return false; }
        uint32_t old = 0;
        if (!protect(va, rounded, flags, old)) { unmapFile(va, rounded); return false; }
        return true;
    }
    bool unmapFile(uint32_t va, uint64_t bytes) {
        if (!va || !bytes || va % page_ || bytes > size - va) return false;
        uint64_t rounded = (bytes + page_ - 1) / page_ * page_;
        if (mmap(address(va, rounded), rounded, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) return false;
        return decommit(va, rounded);
    }
    // x86 execute permissions are guest metadata. ARM64 never executes these bytes.
    bool protect(uint32_t va,uint64_t bytes,uint32_t flags,uint32_t& previous){
        if(!bytes||uint64_t(va)>=size||bytes>size-va)return false;
        if(flags!=1&&flags!=2&&flags!=4&&flags!=8&&flags!=0x10&&flags!=0x20&&flags!=0x40&&flags!=0x80)return false;
        uint64_t start=uint64_t(va)/page_*page_,end=(uint64_t(va)+bytes+page_-1)/page_*page_;
        auto region=committed_.upper_bound(start);if(region==committed_.begin())return false;
        --region;if(end>region->second)return false;
        int native=flags==1?PROT_NONE:PROT_READ;
        if(flags==4||flags==8||flags==0x40||flags==0x80)native|=PROT_WRITE;
        auto old=protection_.find(start);uint32_t first=old==protection_.end()?4:old->second;
        if(mprotect(static_cast<unsigned char*>(base_)+start,end-start,native))return false;
        for(uint64_t page=start;page<end;page+=page_)protection_[page]=flags;
        previous=first;return true;
    }
    void* address(uint32_t va, uint64_t bytes) const {
        if (uint64_t(va) >= size || bytes > size - va) throw std::out_of_range("Guest range outside the guest address space");
        return static_cast<unsigned char*>(base_) + va;
    }
    void commit(uint32_t va, uint64_t bytes) {
        if (!bytes) throw std::invalid_argument("Empty guest commit");
        address(va, bytes);
        const uint64_t page = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
        const uint64_t start = uint64_t(va) / page * page;
        const uint64_t end = (uint64_t(va) + bytes + page - 1) / page * page;
        // Keep page zero inaccessible to diagnose guest null pointers.
        if (!start) throw std::invalid_argument("Guest null page cannot be committed");
        // Heap blocks share host pages. Committing a neighboring block must
        // preserve protections already applied to an existing page.
        uint64_t cursor=start;auto existing=committed_.upper_bound(start);
        if(existing!=committed_.begin()){auto before=std::prev(existing);if(before->second>start)existing=before;}
        while(cursor<end){
            if(existing!=committed_.end()&&existing->first<=cursor){cursor=std::max(cursor,existing->second);++existing;continue;}
            uint64_t limit=existing==committed_.end()?end:std::min(end,existing->first);
            if(mprotect(static_cast<unsigned char*>(base_)+cursor,limit-cursor,PROT_READ|PROT_WRITE))throw std::runtime_error("Guest commit failed");
            cursor=limit;
        }
        uint64_t mergedStart=start,mergedEnd=end;
        auto region=committed_.lower_bound(start);
        if(region!=committed_.begin()){auto previous=std::prev(region);if(previous->second>=start)region=previous;}
        while(region!=committed_.end()&&region->first<=mergedEnd){mergedStart=std::min(mergedStart,region->first);mergedEnd=std::max(mergedEnd,region->second);region=committed_.erase(region);}
        committed_.emplace(mergedStart,mergedEnd);
    }
};

inline void testGuestMemory() {
    GuestMemory mem;
    // Exercise the game image range and addresses with the high guest bit set.
    for (uint32_t va : {0x00400000u, uint32_t(GuestMemory::size/2), uint32_t(GuestMemory::size-0x10000u)}) {
        mem.commit(va, 4);
        const uint32_t expected = va ^ 0x12345678u;
        std::memcpy(mem.address(va, 4), &expected, 4);
        uint32_t actual = 0; std::memcpy(&actual, mem.address(va, 4), 4);
        if (actual != expected) throw std::runtime_error("Guest memory round trip failed");
    }
    bool rejected = false;
    try { mem.address(uint32_t(GuestMemory::size-1), 2); } catch (const std::out_of_range&) { rejected = true; }
    if (!rejected) throw std::runtime_error("Guest wraparound was accepted");
    if(mem.readable(0,1)||mem.readable(0x30000000,1)||!mem.readable(0x400000,4)||mem.readable(uint32_t(GuestMemory::size-1),2))throw std::runtime_error("Guest committed range validation failed");
    const uint32_t test = 0x50000000;
    mem.commit(test, 65536);
    uint32_t marker = 0x12345678;
    std::memcpy(mem.address(test, 4), &marker, 4);
    auto committed = mem.query(test);
    if (committed.state != 0x1000 || committed.protection != 4 || committed.bytes < 65536)
        throw std::runtime_error("Guest committed region query failed");
    uint32_t previous = 0;
    if (!mem.protect(test, 65536, 2, previous) || previous != 4 || mem.writable(test, 1) || !mem.readable(test, 1))
        throw std::runtime_error("Guest protection state failed");
    if (!mem.decommit(test, 65536) || mem.readable(test, 1) || mem.query(test).state != 0x10000)
        throw std::runtime_error("Guest decommit/query failed");
    mem.commit(test, 65536);
    if (!mem.writable(test, 65536)) throw std::runtime_error("Guest recommit failed");
    std::memcpy(&marker, mem.address(test, 4), 4);
    if (marker) throw std::runtime_error("Guest recommit retained discarded contents");
}
