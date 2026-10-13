#include <android/log.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <chrono>
#include <cstdio>
#include <cctype>
#include <ctime>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <array>
#include <thread>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <cerrno>
#include <deque>
#include "guest_heap.h"
#include "pe_version.h"
#include "guest_cpu.h"
#include "vulkan_renderer.h"
#include "dx7_guest.h"
#include "guest_machine.h"
extern "C" {
#include "recomp_types.h"
}

namespace {
constexpr uint32_t kStackBase = 0x00300000;
constexpr uint32_t kStackSize = 0x00100000;
constexpr uint32_t kTibBase = 0x00200000;
constexpr uint32_t kImportBase = 0x70000000;
const PeImageInfo* activeInfo = nullptr;
std::deque<PeImageInfo::Import> activeImports;
PeImage* activeImage = nullptr;
GuestMemory* memory = nullptr;
std::string hostGameRoot;
uint32_t activeImport = 0;
std::unordered_map<uint32_t, uint64_t> importCounts;
std::chrono::steady_clock::time_point deadline;
std::unique_ptr<GuestHeaps> heaps;
std::mutex machineLock;
ANativeWindow* presentationWindow=nullptr;
std::shared_ptr<GuestRunControl> runControl;
void pumpAndroidInput();

std::string addressText(uint32_t va) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08x", va);
    return buf;
}
void progressCheck() {
    if(runControl&&runControl->stopped.load())throw std::runtime_error("Sesion detenida");
    pumpAndroidInput();
    if (!runControl&&std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error("Tiempo de arranque agotado en 0x" + addressText(g_cur_func));
}
void importBridge();
recomp_func_t graphicsLookup(uint32_t va);
recomp_func_t inputLookup(uint32_t va);
}

extern "C" {
uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
double g_st[8];
int g_fp_top;
uint16_t g_fpu_cw = 0x027f;
uint16_t g_seg_cs, g_seg_ds, g_seg_es, g_seg_fs, g_seg_gs, g_seg_ss;
uint64_t g_mm[8];
V128 g_xmm[8];
uint32_t g_mxcsr = 0x1f80;
uint32_t g_fs_base, g_gs_base, g_cur_func;
ptrdiff_t g_mem_base;
uint32_t g_icall_trace[ICALL_TRACE_SIZE], g_icall_from[ICALL_TRACE_SIZE];
uint32_t g_icall_trace_idx, g_icall_count;

recomp_func_t recomp_lookup(uint32_t va) {
    uint32_t lo = 0, hi = recomp_dispatch_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t candidate = recomp_dispatch_table[mid].address;
        if (candidate == va) return recomp_dispatch_table[mid].func;
        if (candidate < va) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}

recomp_func_t recomp_lookup_manual(uint32_t) { return nullptr; }
recomp_func_t recomp_lookup_import(uint32_t va) {
    progressCheck();
    if(auto function=graphicsLookup(va))return function;
    if(auto function=inputLookup(va))return function;
    if (!activeInfo || va < kImportBase || (va - kImportBase) % 16) return nullptr;
    uint32_t index = (va - kImportBase) / 16;
    if (index >= activeImports.size()) return nullptr;
    activeImport = index;
    return importBridge;
}

void recomp_unresolved(const char* kind, uint32_t va, uint32_t from) {
    throw std::runtime_error(std::string(kind) + " sin resolver 0x" + addressText(va) +
                             " desde 0x" + addressText(from));
}

void recomp_unimpl(uint32_t va, const char* instruction) {
    __android_log_print(ANDROID_LOG_ERROR, "AMRTS", "Unsupported x86 instruction at %08x: %s", va, instruction);
    throw std::runtime_error("Unsupported lifted instruction at " + std::to_string(va));
}
}

namespace {
#include "guest_state.h"
constexpr uint32_t guestThreadId = 1;
uint32_t callGuest(uint32_t va, const std::vector<uint32_t>& args);
uint32_t allocate(uint32_t bytes) {
    uint32_t address = heaps->alloc(GuestHeaps::process, 8, bytes);
    if (!address) throw std::runtime_error("Guest process heap exhausted");
    return address;
}
void checkGuestProgress() { progressCheck(); }
void yieldGuestMachine(unsigned ms) { progressCheck(); if (ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
#include "guest_kernel.inc"
uint32_t callGuest(uint32_t va, const std::vector<uint32_t>& args) {
    MachineState state;
    try {
        auto function = recomp_lookup_manual(va);
        if (!function) function = recomp_lookup(va);
        if (!function) function = recomp_lookup_import(va);
        if (!function) throw GuestStop("Missing guest callback at 0x" + addressText(va));
        for (auto it=args.rbegin();it!=args.rend();++it) { g_esp-=4; write32(g_esp,*it); }
        g_esp-=4;write32(g_esp,RECOMP_RETADDR);function();
        if (g_esp != state.registers[3]) throw GuestStop("Guest callback stack imbalance at 0x" + addressText(va));
        uint32_t result=g_eax;state.restore();return result;
    } catch (...) { state.restore();throw; }
}
#include "guest_kernel_tests.inc"
#include "guest_input_tests.inc"
struct BootCleanup {
    ~BootCleanup() {
        recomp_yield_hook = nullptr;
        activeInfo = nullptr;
        activeImports.clear();
        importCounts.clear();
        resetKernel();
        activeImage = nullptr;
        memory = nullptr;
        heaps.reset();
        runControl.reset();presentationWindow=nullptr;
        g_mem_base = 0;
    }
};
void importBridge() {
    const auto& imported = activeImports.at(activeImport);
    if (++importCounts[imported.target] == 1)
        __android_log_print(ANDROID_LOG_INFO, "AMRTS", "Import %s!%s from %08x", imported.library.c_str(), imported.name.c_str(), g_cur_func);
    const uint32_t argument = MEM32(g_esp + 4);
    auto arg = [](uint32_t index) { return MEM32(g_esp + 4 + index * 4); };
    auto ret = [](uint32_t value, uint32_t count) { g_eax = value; g_esp += 4 + count * 4; };
    if (kernelDispatch(imported.name)) return;
    if (imported.name == "GetVersion") {
        ret(0x0a280105, 0); // Windows XP 5.1 build 2600, NT version encoding.
    } else if (imported.name == "GetVersionExA") {
        uint32_t size = MEM32(argument);
        if (size != 148 && size != 156) {
            ret(0, 1);
        } else {
            std::memset(activeImage->address(argument + 4, size - 4), 0, size - 4);
            MEM32(argument + 4) = 5;
            MEM32(argument + 8) = 1;
            MEM32(argument + 12) = 2600;
            MEM32(argument + 16) = 2;
            if (size == 156) MEM8(argument + 154) = 1;
            ret(1, 1);
        }
    } else if (imported.name == "GetCommandLineA") {
        ret(kTibBase + 0x1000, 0);
    } else if (imported.name == "GetStartupInfoA") {
        std::memset(activeImage->address(argument, 68), 0, 68);
        MEM32(argument) = 68;
        ret(0, 1);
    } else if (imported.name == "GetProcessHeap") {
        ret(GuestHeaps::process, 0);
    } else if (imported.name == "HeapCreate") {
        ret(heaps->create(arg(0), arg(1), arg(2)), 3);
    } else if (imported.name == "HeapDestroy") {
        ret(heaps->destroy(arg(0)), 1);
    } else if (imported.name == "HeapAlloc") {
        ret(heaps->alloc(arg(0), arg(1), arg(2)), 3);
    } else if (imported.name == "HeapFree") {
        ret(heaps->free(arg(0), arg(2)), 3);
    } else if (imported.name == "HeapSize") {
        ret(heaps->size(arg(0), arg(2)), 3);
    } else if (imported.name == "HeapReAlloc") {
        ret(heaps->realloc(arg(0), arg(1), arg(2), arg(3)), 4);
    } else {
        throw std::runtime_error("Win32 pendiente: " + imported.library + "!" + imported.name +
                                 " desde 0x" + addressText(g_cur_func));
    }
}
}

std::string probeGuestBoot(PeImage& image, const PeImageInfo& info,ANativeWindow* window,std::shared_ptr<GuestRunControl> control) {
    std::unique_lock<std::mutex> lock(machineLock,std::try_to_lock);
    if(!lock.owns_lock())return "Ya hay una sesion activa";
    if (activeImage) return "Ya hay una maquina x86 activa";
    BootCleanup cleanup;
    presentationWindow=window;
    runControl=std::move(control);
    image.commit(kStackBase, kStackSize);
    image.commit(kTibBase, 0x2000);
    std::memcpy(image.address(kTibBase + 0x1000, 10), "AMRTS.EXE", 10);
    activeImage = &image;
    memory = &image.memory();
    hostGameRoot = std::filesystem::path(info.sourcePath).parent_path().string();
    activeInfo = &info;
    activeImports.assign(info.importTable.begin(),info.importTable.end());
    g_mem_base = reinterpret_cast<ptrdiff_t>(image.base());
    g_eax = g_ecx = g_edx = g_ebx = g_esi = g_edi = g_ebp = 0;
    g_esp = kStackBase + kStackSize - 4;
    MEM32(g_esp) = RECOMP_RETADDR;
    g_fs_base = kTibBase;
    MEM32(kTibBase) = UINT32_MAX;
    MEM32(kTibBase + 4) = kStackBase + kStackSize;
    MEM32(kTibBase + 8) = kStackBase;
    MEM32(kTibBase + 0x18) = kTibBase;
    MEM32(kTibBase + 0x20) = MEM32(kTibBase + 0x24) = 1;
    MEM32(kTibBase + 0x30) = kTibBase + 0x1800;
    MEM32(kTibBase + 0x1808) = info.imageBase;
    MEM32(kTibBase + 0x1818) = GuestHeaps::process;
    g_gs_base = 0;
    g_cur_func = 0;
    g_fp_top = 0;
    g_fpu_cw = 0x027f;
    g_flag_k=g_flag_a=g_flag_b=g_flag_cf=0;
    g_backedges=0;g_mxcsr=0x1f80;
    std::memset(g_st,0,sizeof g_st);std::memset(g_st_i64,0,sizeof g_st_i64);
    std::memset(g_mm,0,sizeof g_mm);std::memset(g_xmm,0,sizeof g_xmm);
    g_icall_count = g_icall_trace_idx = 0;
    testGuestHeaps(image.memory());
    testGuestMemory();
    heaps = std::make_unique<GuestHeaps>(image.memory());
    resetKernel();
    testKernelServices();
    testGuestInput();
    GuestCpuResult cpu = guestCpuQuery(0);
    if (cpu.b != 0x756e6547 || !(guestCpuQuery(1).d & (1u << 4))) throw GuestStop("Virtual x86 CPUID test failed");
    {
        MachineState before;
        g_eax=0x12345678;g_esi=0x87654321;g_st[0]=123.5;g_st_i64[0]=123;
        uint32_t stack=g_esp;
        callGuest(0x00439810,{});
        if(g_esp!=stack||g_eax!=0x12345678||g_esi!=0x87654321||g_st[0]!=123.5||g_st_i64[0]!=123)
            throw GuestStop("ARM64 guest callback register/x87 preservation failed");
        float expected=MEMF(0x0066abc4)*2.0f;
        if(MEMF(0x0072042c)!=expected)throw GuestStop("ARM64 lifted initializer result mismatch");
        before.restore();
        __android_log_print(ANDROID_LOG_INFO,"AMRTS","ARM64 callback and virtual CPUID tests passed");
    }
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    recomp_yield_hook = progressCheck;
    std::string result;
    try {
        auto entry = recomp_lookup(info.entry);
        if (!entry) throw std::runtime_error("La entrada no esta recompilada");
        entry();
        result = "La entrada retorno con EAX=0x" + addressText(g_eax);
    } catch (const std::exception& error) {
        result = error.what();
    }
    return result;
}
