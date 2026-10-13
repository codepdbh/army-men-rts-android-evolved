// Included inside guest_machine's namespace. All pointers are guest VAs.
uint32_t lastError = 0, exceptionFilter = 0;
int32_t virtualThreadPriority = 0;
uint32_t virtualProcessPriority = 0x20;
std::array<bool, 1088> tlsUsed{};
std::array<uint32_t, 1088> tlsValues{};
struct CriticalSection { std::recursive_mutex mutex; uint32_t count = 0; };
std::unordered_map<uint32_t, std::unique_ptr<CriticalSection>> criticalSections;
uint32_t environmentA = 0, environmentW = 0;
std::unordered_map<std::string, uint32_t> modules;
std::string currentDirectory = "C:\\armymenrts";
void resetVirtual();
void resetSync();
void resetFiles();
void resetTimers();
void resetVersions();
void resetWindows();
void resetMappings();
void resetGraphics();
void resetInput();
void resetRegistry();
bool queryMappedMemory(uint32_t,uint32_t&,uint32_t&,uint64_t&,uint32_t&);

uint32_t kernelArg(uint32_t index) { return MEM32(g_esp + 4 + index * 4); }
void kernelReturn(uint32_t value, uint32_t count) { g_eax = value; g_esp += 4 + count * 4; }
void* kernelPtr(uint32_t va, uint64_t bytes, bool write = false) {
    if (write ? !activeImage->memory().writable(va, bytes) : !activeImage->memory().readable(va, bytes))
        throw std::runtime_error("Win32 pointer outside committed memory: 0x" + addressText(va));
    return activeImage->address(va, bytes);
}
void kernelError(uint32_t value) { lastError = value; MEM32(kTibBase + 0x34) = value; }
void resetKernel() {
    lastError = exceptionFilter = environmentA = environmentW = 0;
    virtualThreadPriority = 0; virtualProcessPriority = 0x20;
    tlsUsed.fill(false); tlsValues.fill(0);
    for (auto& entry : criticalSections)
        while (entry.second->count) { --entry.second->count; entry.second->mutex.unlock(); }
    criticalSections.clear();
    modules.clear(); currentDirectory = "C:\\armymenrts";
    resetVirtual();
    resetSync();
    resetFiles();
    resetTimers();
    resetVersions();
    resetWindows();
    resetMappings();
    resetGraphics();
    resetInput();
    resetRegistry();
    if (activeInfo) {
        for (const auto& dll : activeInfo->importedLibraries) {
            std::string name = dll;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (!modules.count(name)) modules[name] = 0x71000000u + uint32_t(modules.size()) * 0x10000;
        }
        modules.emplace("ddraw.dll", 0x71000000u + uint32_t(modules.size()) * 0x10000);
        modules.emplace("dinput.dll", 0x71000000u + uint32_t(modules.size()) * 0x10000);
    }
}

uint32_t arg(uint32_t index) { return kernelArg(index); }
void ret(uint32_t value, uint32_t count) { kernelReturn(value, count); }
void* ptr(uint32_t va, uint64_t bytes) { return kernelPtr(va, bytes); }
void write32(uint32_t va, uint32_t value) { std::memcpy(kernelPtr(va, 4, true), &value, 4); }
uint32_t read32(uint32_t va) { uint32_t value; std::memcpy(&value, kernelPtr(va, 4), 4); return value; }
using GuestStop = std::runtime_error;
std::string hex(uint32_t va) { return addressText(va); }
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}
std::string guestString(uint32_t va) {
    std::string result;
    for (uint32_t i = 0; i < 32768; ++i) {
        char c = *static_cast<const char*>(kernelPtr(va + i, 1));
        if (!c) return result;
        result.push_back(c);
    }
    throw GuestStop("Unterminated guest string");
}
std::vector<uint16_t> wideString(uint32_t va) {
    std::vector<uint16_t> result;
    for (uint32_t i = 0; i < 32768; ++i) {
        uint16_t c; std::memcpy(&c, kernelPtr(va + i * 2, 2), 2);
        if (!c) return result;
        result.push_back(c);
    }
    throw GuestStop("Unterminated guest UTF16 string");
}
#include "guest_charset.inc"
#include "guest_virtual.inc"
#include "guest_sync.inc"
#include "guest_files.inc"
#include "guest_timer.inc"
#include "guest_version.inc"
#include "guest_window.inc"
#include "guest_format.inc"
#include "guest_window_services.inc"
#include "guest_mapping.inc"
#include "guest_ddraw.inc"
#include "guest_dinput.inc"
#include "guest_registry.inc"

std::string moduleName(std::string name) {
    auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (name.find('.') == std::string::npos) name += ".dll";
    return name;
}

bool kernelDispatch(const std::string& name) {
    if (virtualDispatch(name)) return true;
    if (mappingDispatch(name)) return true;
    if (filesDispatch(name)) return true;
    if (syncDispatch(name)) return true;
    if (timerDispatch(name)) return true;
    if (versionDispatch(name)) return true;
    if (windowDispatch(name)) return true;
    if (ddrawDispatch(name)) return true;
    if (inputDispatch(name)) return true;
    if (registryDispatch(name)) return true;
    // Report a missing audio device through Miles' normal failure path.
    // No sample handle is returned until Android audio output exists.
    if(name=="_AIL_set_redist_directory@4"){ret(0,1);return true;}
    if(name=="_AIL_startup@0"||name=="_AIL_shutdown@0"){ret(0,0);return true;}
    if(name=="_AIL_open_digital_driver@16"){ret(0,4);return true;}
    if(name=="_AIL_last_error@0"){
        const char message[]="Android audio output is not implemented yet";uint32_t output=allocate(sizeof message);
        std::memcpy(kernelPtr(output,sizeof message,true),message,sizeof message);ret(output,0);return true;
    }
    if(name=="_AIL_ms_count@0"){ret(uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()),0);return true;}
    // The game handles unavailable movie decoding by skipping the intro.
    if(name=="_BinkOpen@8"){ret(0,2);return true;}
    if(name=="_BinkGetError@0"){
        const char message[]="Bink decoding is not implemented on Android yet";uint32_t output=allocate(sizeof message);
        std::memcpy(kernelPtr(output,sizeof message,true),message,sizeof message);ret(output,0);return true;
    }
    auto arg = kernelArg;
    auto ret = kernelReturn;
    if (name == "InitializeCriticalSection" || name == "InitializeCriticalSectionAndSpinCount") {
        uint32_t address = arg(0);
        auto found = criticalSections.find(address);
        if (found != criticalSections.end() && found->second->count)
            throw std::runtime_error("Reinitializing an active critical section");
        std::memset(kernelPtr(address, 24, true), 0, 24);
        MEM32(address + 4) = UINT32_MAX;
        MEM32(address + 20) = name == "InitializeCriticalSection" ? 0 : arg(1);
        criticalSections[address] = std::make_unique<CriticalSection>();
        ret(name == "InitializeCriticalSection" ? 0 : 1, name == "InitializeCriticalSection" ? 1 : 2);
    } else if (name == "EnterCriticalSection" || name == "TryEnterCriticalSection") {
        uint32_t address = arg(0);
        auto found = criticalSections.find(address);
        if (found == criticalSections.end()) throw std::runtime_error("Critical section not initialized");
        if (!found->second->mutex.try_lock()) {
            if (name == "TryEnterCriticalSection") { ret(0, 1); return true; }
            throw std::runtime_error("Contended critical section needs guest thread scheduler");
        }
        uint32_t count = ++found->second->count;
        MEM32(address + 4) = count - 1; MEM32(address + 8) = count; MEM32(address + 12) = 1;
        ret(name == "TryEnterCriticalSection" ? 1 : 0, 1);
    } else if (name == "LeaveCriticalSection") {
        uint32_t address = arg(0);
        auto found = criticalSections.find(address);
        if (found == criticalSections.end() || !found->second->count)
            throw std::runtime_error("Unbalanced critical-section release");
        uint32_t count = --found->second->count;
        MEM32(address + 4) = count - 1; MEM32(address + 8) = count;
        if (!count) MEM32(address + 12) = 0;
        found->second->mutex.unlock(); ret(0, 1);
    } else if (name == "DeleteCriticalSection") {
        auto found = criticalSections.find(arg(0));
        if (found != criticalSections.end() && found->second->count)
            throw std::runtime_error("Deleting an active critical section");
        criticalSections.erase(arg(0)); ret(0, 1);
    } else if (name == "TlsAlloc") {
        uint32_t slot = 0;
        while (slot < tlsUsed.size() && tlsUsed[slot]) ++slot;
        if (slot == tlsUsed.size()) { kernelError(259); ret(UINT32_MAX, 0); }
        else { tlsUsed[slot] = true; tlsValues[slot] = 0; ret(slot, 0); }
    } else if (name == "TlsGetValue" || name == "TlsSetValue" || name == "TlsFree") {
        uint32_t slot = arg(0);
        if (slot >= tlsUsed.size() || !tlsUsed[slot]) {
            kernelError(87); ret(0, name == "TlsSetValue" ? 2 : 1);
        } else if (name == "TlsGetValue") { kernelError(0); ret(tlsValues[slot], 1); }
        else if (name == "TlsSetValue") { tlsValues[slot] = arg(1); ret(1, 2); }
        else { tlsUsed[slot] = false; tlsValues[slot] = 0; ret(1, 1); }
    } else if (name == "GetLastError") { ret(lastError, 0);
    } else if (name == "SetLastError") { kernelError(arg(0)); ret(0, 1);
    } else if (name == "GetCurrentThreadId" || name == "GetCurrentProcessId") { ret(1, 0);
    } else if (name == "GetCurrentThread") { ret(0xfffffffe, 0);
    } else if (name == "GetCurrentProcess") { ret(UINT32_MAX, 0);
    } else if (name == "GetThreadPriority" || name == "SetThreadPriority") {
        if (arg(0) != 0xfffffffeu) { kernelError(6); ret(name == "GetThreadPriority" ? 0x7fffffffu : 0, name == "GetThreadPriority" ? 1 : 2); }
        else if (name == "GetThreadPriority") ret(uint32_t(virtualThreadPriority), 1);
        else { int32_t value = int32_t(arg(1));
            if ((value < -2 || value > 2) && value != -15 && value != 15) { kernelError(87); ret(0, 2); }
            else { virtualThreadPriority = value; ret(1, 2); } }
    } else if (name == "GetPriorityClass" || name == "SetPriorityClass") {
        if (arg(0) != UINT32_MAX) { kernelError(6); ret(0, name == "GetPriorityClass" ? 1 : 2); }
        else if (name == "GetPriorityClass") ret(virtualProcessPriority, 1);
        else { uint32_t value = arg(1);
            if (value != 0x20 && value != 0x40 && value != 0x80 && value != 0x100 && value != 0x4000 && value != 0x8000) { kernelError(87); ret(0, 2); }
            else { virtualProcessPriority = value; ret(1, 2); } }
    } else if (name == "GetComputerNameA" || name == "GetUserNameA") {
        std::string text = name == "GetComputerNameA" ? "Android" : "Player";
        uint32_t capacity = read32(arg(1));
        if (capacity <= text.size()) { write32(arg(1), uint32_t(text.size() + 1)); kernelError(name == "GetComputerNameA" ? 111 : 122); ret(0, 2); }
        else { std::memcpy(kernelPtr(arg(0), text.size() + 1, true), text.c_str(), text.size() + 1);
            write32(arg(1), uint32_t(text.size() + (name == "GetUserNameA"))); ret(1, 2); }
    } else if (name == "GetACP") { ret(1252, 0);
    } else if (name == "GetOEMCP") { ret(437, 0);
    } else if (name == "GetUserDefaultLangID" || name == "GetUserDefaultLCID" || name == "GetThreadLocale") { ret(0x0c0a, 0);
    } else if (name == "IsValidCodePage") { ret(arg(0) == 1252 || arg(0) == 437 || arg(0) == 65001, 1);
    } else if (name == "IsValidLocale") { ret(arg(0) == 0x0c0a || arg(0) == 0x409 || arg(0) == 0x7f, 2);
    } else if (name == "GetCPInfo") {
        uint32_t cp = arg(0);
        if (cp != 0 && cp != 1252 && cp != 437 && cp != 65001) { kernelError(87); ret(0, 2); }
        else { std::memset(kernelPtr(arg(1), 20, true), 0, 20); MEM32(arg(1)) = cp == 65001 ? 4 : 1;
            MEM8(arg(1) + 4) = '?'; ret(1, 2); }
    } else if (name == "WideCharToMultiByte") { wideToMulti();
    } else if (name == "MultiByteToWideChar") { multiToWide();
    } else if (name == "GetStringTypeW") { stringTypeW();
    } else if (name == "LCMapStringW") { mapStringW();
    } else if (name == "GetModuleFileNameA") {
        if (arg(0) && arg(0) != activeInfo->imageBase) { kernelError(126); ret(0, 3); }
        else {
            std::string path = "C:\\armymenrts\\AMRTS.EXE";
            uint32_t capacity = arg(2), count = std::min(capacity, uint32_t(path.size()));
            if (count) std::memcpy(kernelPtr(arg(1), count, true), path.data(), count);
            if (capacity > count) MEM8(arg(1) + count) = 0; else kernelError(122);
            ret(count, 3);
        }
    } else if (name == "GetModuleHandleA" || name == "LoadLibraryA") {
        if (!arg(0) && name == "GetModuleHandleA") ret(activeInfo->imageBase, 1);
        else {
            std::string dll = moduleName(guestString(arg(0)));
            auto found = modules.find(dll);
            if (dll == "amrts.exe") ret(activeInfo->imageBase, 1);
            else if (found == modules.end()) { kernelError(126); ret(0, 1); }
            else ret(found->second, 1);
        }
    } else if (name == "FreeLibrary") {
        bool found = std::any_of(modules.begin(), modules.end(), [&](const auto& entry) { return entry.second == arg(0); });
        if (!found) kernelError(6); ret(found, 1);
    } else if (name == "GetProcAddress") {
        auto module = std::find_if(modules.begin(), modules.end(), [&](const auto& entry) { return entry.second == arg(0); });
        if (module == modules.end()) { kernelError(126); ret(0, 2); return true; }
        std::string symbol = arg(1) <= 0xffff ? "#" + std::to_string(arg(1)) : guestString(arg(1));
        for (const auto& imported : activeImports) {
            if (moduleName(imported.library) == module->first && imported.name == symbol) {
                ret(imported.target, 2); return true;
            }
        }
        bool extra = module->first == "kernel32.dll" &&
            (symbol == "InitializeCriticalSectionAndSpinCount" || symbol == "TryEnterCriticalSection");
        extra |= module->first == "ddraw.dll" &&
            (symbol == "DirectDrawCreate" || symbol == "DirectDrawCreateEx" || symbol == "DirectDrawEnumerateExA" || symbol == "DirectDrawEnumerateA");
        extra |= module->first == "dinput.dll" && symbol == "DirectInputCreateEx";
        if (!extra) { kernelError(127); ret(0, 2); }
        else {
            uint32_t target = kImportBase + uint32_t(activeImports.size()) * 16;
            activeImports.push_back({0, target, module->first, symbol}); ret(target, 2);
        }
    } else if (name == "GetCurrentDirectoryA") {
        uint32_t capacity = arg(0);
        if (capacity <= currentDirectory.size()) ret(uint32_t(currentDirectory.size() + 1), 2);
        else { std::memcpy(kernelPtr(arg(1), currentDirectory.size() + 1, true), currentDirectory.c_str(), currentDirectory.size() + 1);
            ret(uint32_t(currentDirectory.size()), 2); }
    } else if (name == "SetCurrentDirectoryA") {
        std::string directory = guestString(arg(0));
        if (directory == "C:\\armymenrts" || directory == "C:\\armymenrts\\" || directory == ".") ret(1, 1);
        else { kernelError(3); ret(0, 1); }
    } else if (name == "GetSystemInfo") {
        uint32_t out = arg(0); std::memset(kernelPtr(out, 36, true), 0, 36);
        MEM32(out + 4) = 4096; MEM32(out + 8) = 0x10000; MEM32(out + 12) = 0x7ffeffff;
        MEM32(out + 16) = 1; MEM32(out + 20) = 1; MEM32(out + 24) = 586;
        MEM32(out + 28) = 65536; MEM16(out + 32) = 6; ret(0, 1);
    } else if (name == "GlobalMemoryStatus") {
        uint32_t out = arg(0); std::memset(kernelPtr(out, 32, true), 0, 32);
        MEM32(out) = 32; MEM32(out + 4) = 25; MEM32(out + 8) = 512u << 20;
        MEM32(out + 12) = 384u << 20; MEM32(out + 16) = 512u << 20;
        MEM32(out + 20) = 384u << 20; MEM32(out + 24) = 0x7fff0000; MEM32(out + 28) = 0x70000000;
        ret(0, 1);
    } else if (name == "GetEnvironmentStrings" || name == "GetEnvironmentStringsA" || name == "GetEnvironmentStringsW") {
        bool wide = name == "GetEnvironmentStringsW";
        uint32_t& address = wide ? environmentW : environmentA;
        if (!address) address = heaps->alloc(GuestHeaps::process, 8, wide ? 4 : 2);
        ret(address, 0);
    } else if (name == "FreeEnvironmentStringsA" || name == "FreeEnvironmentStringsW") {
        uint32_t& address = name == "FreeEnvironmentStringsW" ? environmentW : environmentA;
        bool valid = address && arg(0) == address;
        if (valid) { heaps->free(GuestHeaps::process, address); address = 0; }
        ret(valid, 1);
    } else if (name == "SetHandleCount") { ret(arg(0), 1);
    } else if (name == "GetStdHandle") {
        uint32_t kind = arg(0); ret(kind >= 0xfffffff4u && kind <= 0xfffffff6u ? 0xf0000200u + (0xfffffff6u - kind) * 4 : UINT32_MAX, 1);
    } else if (name == "GetFileType") {
        uint32_t handle = arg(0); if (handle >= 0xf0000200u && handle <= 0xf0000208u) ret(2, 1);
        else { kernelError(6); ret(0, 1); }
    } else if (name == "SetUnhandledExceptionFilter") {
        uint32_t previous = exceptionFilter; exceptionFilter = arg(0); ret(previous, 1);
    } else if (name == "InterlockedIncrement" || name == "InterlockedDecrement" || name == "InterlockedExchange") {
        uint32_t address = arg(0); kernelPtr(address, 4, true); uint32_t old = MEM32(address);
        uint32_t value = name == "InterlockedExchange" ? arg(1) : old + (name == "InterlockedIncrement" ? 1 : UINT32_MAX);
        MEM32(address) = value; ret(name == "InterlockedExchange" ? old : value, name == "InterlockedExchange" ? 2 : 1);
    } else if (name == "QueryPerformanceCounter" || name == "QueryPerformanceFrequency") {
        uint64_t value = name == "QueryPerformanceFrequency" ? 1000000000ULL :
            uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        std::memcpy(kernelPtr(arg(0), 8, true), &value, 8); ret(1, 1);
    } else if (name == "RaiseException") {
        uint32_t code=arg(0);
        if(code==0x406d1388){ret(0,4);return true;}
        std::string detail;
        if(memory->readable(0x0072a528,1)){
            for(uint32_t i=0;i<512&&memory->readable(0x0072a528+i,1);++i){
                char c=MEM8(0x0072a528+i);if(!c)break;
                if(static_cast<unsigned char>(c)<32&&c!='\n'&&c!='\t'){detail.clear();break;}detail+=c;
            }
        }
        for(uint32_t i=0;i<std::min(g_icall_trace_idx,8u);++i){
            uint32_t slot=(g_icall_trace_idx-1-i)&(ICALL_TRACE_SIZE-1);
            __android_log_print(ANDROID_LOG_ERROR,"AMRTS","Exception trail: %08x from %08x",g_icall_trace[slot],g_icall_from[slot]);
        }
        throw GuestStop("Guest exception 0x"+hex(code)+" from 0x"+hex(g_cur_func)+(detail.empty()?std::string{}:": "+detail));
    } else if (name == "ExitProcess" || name == "TerminateProcess") {
        throw std::runtime_error("Guest requested " + name + " at 0x" + addressText(g_cur_func));
    } else if (name == "HeapValidate") {
        uint32_t address = arg(2);
        ret(address ? heaps->size(arg(0), address) != UINT32_MAX : heaps->contains(arg(0)), 3);
    } else { return false; }
    return true;
}
