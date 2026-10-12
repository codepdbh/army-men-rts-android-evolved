void testKernelServices() {
    uint32_t originalStack = g_esp, originalEax = g_eax;
    auto call = [&](const std::string& name, std::initializer_list<uint32_t> args) {
        uint32_t stack = kStackBase + kStackSize - 256;
        g_esp = stack; MEM32(stack) = RECOMP_RETADDR;
        uint32_t i = 0; for (uint32_t value : args) MEM32(stack + 4 + i++ * 4) = value;
        if (!kernelDispatch(name) || g_esp != stack + 4 + args.size() * 4)
            throw GuestStop("Kernel test stack imbalance: " + name);
        return g_eax;
    };
    auto require = [](bool condition, const char* message) { if (!condition) throw GuestStop(message); };
    uint32_t slot = call("TlsAlloc", {});
    require(slot != UINT32_MAX && call("TlsSetValue", {slot, 0x12345678}) == 1,
            "TLS allocation/write failed");
    require(call("TlsGetValue", {slot}) == 0x12345678 && call("TlsFree", {slot}) == 1,
            "TLS round trip/free failed");
    require(call("TlsGetValue", {slot}) == 0 && lastError == 87, "Freed TLS slot accepted");

    uint32_t cs = kTibBase + 0x1200;
    call("InitializeCriticalSection", {cs}); call("EnterCriticalSection", {cs}); call("EnterCriticalSection", {cs});
    require(MEM32(cs + 8) == 2, "Critical-section recursion failed");
    call("LeaveCriticalSection", {cs}); call("LeaveCriticalSection", {cs});
    require(MEM32(cs + 4) == UINT32_MAX && MEM32(cs + 12) == 0, "Critical-section release failed");
    call("DeleteCriticalSection", {cs});

    uint32_t mutex = call("CreateMutexA", {0, 0, 0});
    require(mutex && call("WaitForSingleObject", {mutex, 0}) == 0 && call("WaitForSingleObject", {mutex, 0}) == 0,
            "Mutex recursion failed");
    require(call("ReleaseMutex", {mutex}) && call("ReleaseMutex", {mutex}) && call("CloseHandle", {mutex}),
            "Mutex release/close failed");
    uint32_t event = call("CreateEventA", {0, 0, 1, 0});
    require(event && call("WaitForSingleObject", {event, 0}) == 0 && call("WaitForSingleObject", {event, 0}) == 258,
            "Auto-reset event consumption failed");
    require(call("SetEvent", {event}) && call("WaitForSingleObject", {event, 0}) == 0 && call("CloseHandle", {event}),
            "Event signal/close failed");
    uint32_t semaphore = call("CreateSemaphoreA", {0, 0, 2, 0});
    require(semaphore && call("WaitForSingleObject", {semaphore, 0}) == 258 &&
            call("ReleaseSemaphore", {semaphore, 1, 0}) && call("WaitForSingleObject", {semaphore, 0}) == 0 &&
            call("WaitForSingleObject", {semaphore, 0}) == 258 && call("CloseHandle", {semaphore}),
            "Semaphore accounting failed");

    uint32_t wide = kTibBase + 0x1300, ansi = kTibBase + 0x1400, restored = kTibBase + 0x1500;
    MEM16(wide) = 0x00f1; MEM16(wide + 2) = 0x20ac; MEM16(wide + 4) = 0;
    require(call("WideCharToMultiByte", {1252, 0, wide, UINT32_MAX, ansi, 8, 0, 0}) == 3 &&
            MEM8(ansi) == 0xf1 && MEM8(ansi + 1) == 0x80 && MEM8(ansi + 2) == 0,
            "CP1252 Spanish/euro encoding failed");
    require(call("MultiByteToWideChar", {1252, 0, ansi, 3, restored, 8}) == 3 &&
            MEM16(restored) == 0x00f1 && MEM16(restored + 2) == 0x20ac,
            "CP1252 round trip failed");
    MEM16(wide) = 0xd83d; MEM16(wide + 2) = 0xde00;
    require(call("WideCharToMultiByte", {65001, 0, wide, 2, ansi, 8, 0, 0}) == 4 &&
            MEM8(ansi) == 0xf0 && MEM8(ansi + 3) == 0x80,
            "UTF16 surrogate to UTF8 failed");
    require(call("MultiByteToWideChar", {65001, 8, ansi, 4, restored, 8}) == 2 &&
            MEM16(restored) == 0xd83d && MEM16(restored + 2) == 0xde00,
            "UTF8 surrogate round trip failed");

    uint32_t output = kTibBase + 0x1600;
    uint32_t allocation = call("VirtualAlloc", {0, 65536, 0x2000, 4});
    require(allocation && call("VirtualQuery", {allocation, output, 28}) == 28 && MEM32(output + 16) == 0x2000,
            "Virtual reserve/query failed");
    require(call("VirtualAlloc", {allocation, 65536, 0x1000, 4}) == allocation &&
            call("VirtualProtect", {allocation, 65536, 2, output}) && MEM32(output) == 4 &&
            call("IsBadWritePtr", {allocation, 1}) == 1,
            "Virtual commit/protection failed");
    require(call("VirtualFree", {allocation, 65536, 0x4000}) &&
            call("VirtualQuery", {allocation, output, 28}) == 28 && MEM32(output + 16) == 0x2000,
            "Virtual decommit/query failed");
    require(call("VirtualFree", {allocation, 0, 0x8000}) &&
            call("VirtualQuery", {allocation, output, 28}) == 28 && MEM32(output + 16) == 0x10000,
            "Virtual release/query failed");

    uint32_t path = kTibBase + 0x1700, data = kTibBase + 0x1760;
    std::memcpy(kernelPtr(path, 10, true), "amrts.exe", 10);
    uint32_t file = call("CreateFileA", {path, 0x80000000, 1, 0, 3, 0, 0});
    require(file != UINT32_MAX && call("GetFileSize", {file, 0}) > 1000000,
            "Case-insensitive game file open/size failed");
    require(call("ReadFile", {file, data, 64, output, 0}) && MEM32(output) == 64 && MEM16(data) == 0x5a4d,
            "Game PE read failed");
    uint32_t mapping = call("CreateFileMappingA", {file, 0, 2, 0, 0, 0});
    require(mapping && call("CloseHandle", {file}), "Mapping file-handle lifetime failed");
    uint32_t view = call("MapViewOfFile", {mapping, 4, 0, 0, 64});
    require(view && MEM16(view) == 0x5a4d && call("VirtualQuery", {view, output, 28}) == 28 &&
            MEM32(output + 24) == 0x40000, "PE mapped view/query failed");
    require(call("CloseHandle", {mapping}) && MEM16(view) == 0x5a4d && call("UnmapViewOfFile", {view}) &&
            call("IsBadReadPtr", {view, 1}) == 1, "Mapped view lifetime/unmap failed");
    mapping = call("CreateFileMappingA", {UINT32_MAX, 0, 4, 0, 65536, 0});
    uint32_t shared = call("MapViewOfFile", {mapping, 2, 0, 0, 0});
    uint32_t copied = call("MapViewOfFile", {mapping, 1, 0, 0, 0});
    require(mapping && shared && copied, "Anonymous mapping creation failed");
    MEM32(shared)=0x1234;require(MEM32(copied)==0x1234,"Mapping shared backing failed");
    MEM32(copied)=0x5678;require(MEM32(shared)==0x1234,"Mapping copy-on-write failed");
    require(call("UnmapViewOfFile", {shared}) && call("UnmapViewOfFile", {copied}) && call("CloseHandle", {mapping}),
            "Anonymous mapping cleanup failed");
    uint32_t versionBytes = call("GetFileVersionInfoSizeA", {path, output});
    require(versionBytes > 52, "PE version resource discovery failed");
    uint32_t version = heaps->alloc(GuestHeaps::process, 0, versionBytes);
    require(version && call("GetFileVersionInfoA", {path, 0, versionBytes, version}), "PE version resource copy failed");
    MEM8(path) = '\\'; MEM8(path + 1) = 0;
    require(call("VerQueryValueA", {version, path, output, output + 4}) && MEM32(output + 4) == 52 &&
            MEM32(MEM32(output)) == 0xfeef04bd, "PE fixed version query failed");
    heaps->free(GuestHeaps::process, version); versionBuffers.erase(version);
    g_esp = originalStack; g_eax = originalEax; kernelError(0);
    __android_log_print(ANDROID_LOG_INFO, "AMRTS", "Win32 tests passed: memory, heap, TLS, sync, charset, files, mappings, PE version");
}
