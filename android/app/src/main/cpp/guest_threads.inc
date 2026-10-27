// Cooperative guest threads.
//
// Lifted code keeps the x86 machine in globals (g_eax..., x87, flags), so only
// one guest thread may run at a time. Each guest thread gets its own host
// thread (its own C stack for nested lifted calls), guest stack, TIB and TLS;
// a fair "machine" lock decides which one runs. A thread hands the machine
// over only at well-defined points (blocking waits, Sleep, contended critical
// sections, and every few milliseconds from the lifted back-edge hook), and
// saves/restores the whole machine state around the hand-over. The lifted
// runtime already spills its register locals to the globals at those points.
struct GuestThread {
    uint32_t id=1,handle=0,tib=kTibBase,stack=0,start=0,parameter=0,exitCode=259;
    bool started=false,finished=false,suspended=false;
    std::array<uint32_t,1088> tls{};
    std::thread host;
};
std::mutex machineMutex;
std::condition_variable machineTurn;
uint64_t machineNext=0,machineServing=0;
std::vector<std::unique_ptr<GuestThread>> guestThreads;
thread_local GuestThread* currentThread=nullptr;
std::atomic<bool> threadsStopping{false};
std::chrono::steady_clock::time_point machineSlice;
uint32_t nextThreadId=2;
constexpr uint32_t threadStackBytes=0x40000;

void machineAcquire(){
    std::unique_lock<std::mutex> lock(machineMutex);
    uint64_t ticket=machineNext++;
    machineTurn.wait(lock,[&]{return machineServing==ticket;});
}
void machineRelease(){
    {std::lock_guard<std::mutex> lock(machineMutex);++machineServing;}
    machineTurn.notify_all();
}
uint32_t currentThreadId(){return currentThread?currentThread->id:1;}
GuestThread* mainThread(){return guestThreads.empty()?nullptr:guestThreads.front().get();}
bool otherThreadsRunnable(){
    for(const auto& thread:guestThreads)if(thread.get()!=currentThread&&thread->started&&!thread->finished&&!thread->suspended)return true;
    return false;
}
// Let other guest threads run; `sleepMs` naps without holding the machine.
void machineYield(unsigned sleepMs){
    if(!otherThreadsRunnable()){
        if(sleepMs)std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        return;
    }
    MachineState state;
    machineRelease();
    if(sleepMs)std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));else std::this_thread::yield();
    machineAcquire();
    state.restore();machineSlice=std::chrono::steady_clock::now();
    if(threadsStopping.load())throw GuestStop("Sesion detenida");
}
// Preemption from progressCheck: a lifted loop waiting on another thread's
// flag never calls a blocking API, so give the others a slice periodically.
void machineTimeSlice(){
    if(guestThreads.size()<2)return;
    auto now=std::chrono::steady_clock::now();
    if(now-machineSlice<std::chrono::milliseconds(2))return;
    machineSlice=now;machineYield(0);
}
GuestThread* threadByHandle(uint32_t handle){
    if(handle==0xfffffffeu)return currentThread;
    for(const auto& thread:guestThreads)if(thread->handle==handle)return thread.get();
    return nullptr;
}
uint32_t threadHandle(GuestThread& thread);
void threadMain(GuestThread* thread){
    machineAcquire();currentThread=thread;machineSlice=std::chrono::steady_clock::now();
    try{
        if(threadsStopping.load())throw GuestStop("Sesion detenida");
        g_eax=g_ecx=g_edx=g_ebx=g_esi=g_edi=g_ebp=0;
        g_esp=thread->stack+threadStackBytes-16;
        g_esp-=4;write32(g_esp,thread->parameter);g_esp-=4;write32(g_esp,RECOMP_RETADDR);
        g_fs_base=thread->tib;g_fp_top=0;g_fpu_cw=0x027f;g_mxcsr=0x1f80;g_flag_k=g_flag_a=g_flag_b=g_flag_cf=0;
        std::memset(g_st,0,sizeof g_st);std::memset(g_st_i64,0,sizeof g_st_i64);
        auto function=recomp_lookup_manual(thread->start);if(!function)function=recomp_lookup(thread->start);
        if(!function)throw GuestStop("Thread start not lifted: 0x"+hex(thread->start));
        __android_log_print(ANDROID_LOG_INFO,"AMRTS","Guest thread %u started at %08x",thread->id,thread->start);
        function();thread->exitCode=g_eax;
    }catch(const std::exception& error){
        if(!threadsStopping.load())__android_log_print(ANDROID_LOG_ERROR,"AMRTS","Guest thread %u stopped: %s",thread->id,error.what());
        thread->exitCode=1;
    }
    thread->finished=true;
    auto found=syncHandles.find(thread->handle);if(found!=syncHandles.end())found->second->signaled=true;
    machineRelease();
}
void launchThread(GuestThread& thread){thread.started=true;thread.host=std::thread(threadMain,&thread);}
uint32_t threadHandle(GuestThread& thread){
    if(!thread.handle){
        auto object=std::make_shared<SyncObject>();object->kind=SyncKind::Event;object->manual=true;object->signaled=thread.finished;
        thread.handle=nextSyncHandle;nextSyncHandle+=4;syncHandles.emplace(thread.handle,object);
    }
    return thread.handle;
}
// The session thread becomes guest thread 1 and holds the machine.
void startGuestThreads(){
    threadsStopping=false;machineNext=machineServing=0;nextThreadId=2;guestThreads.clear();
    auto main=std::make_unique<GuestThread>();main->started=true;currentThread=main.get();guestThreads.push_back(std::move(main));
    machineAcquire();machineSlice=std::chrono::steady_clock::now();
}
// Session end: the session thread holds the machine. Release it so every
// started guest thread wakes at its next hand-over, sees the stop flag and
// unwinds; then join them before the guest memory goes away.
void stopGuestThreads(){
    if(guestThreads.empty())return;
    threadsStopping=true;
    machineRelease();
    for(auto& thread:guestThreads)if(thread->host.joinable())thread->host.join();
    guestThreads.clear();currentThread=nullptr;machineNext=machineServing=0;
}

bool threadDispatch(const std::string& name){
    if(name=="CreateThread"){
        uint32_t stackSize=std::max(arg(1),threadStackBytes),start=arg(2),parameter=arg(3),flags=arg(4),idOut=arg(5);
        auto thread=std::make_unique<GuestThread>();thread->id=nextThreadId++;thread->start=start;thread->parameter=parameter;
        thread->stack=allocate(std::max(stackSize,threadStackBytes));
        // A private TIB: SEH chain, stack limits, thread id and last error.
        thread->tib=allocate(0x2000);std::memset(kernelPtr(thread->tib,0x2000,true),0,0x2000);
        write32(thread->tib,UINT32_MAX);write32(thread->tib+4,thread->stack+threadStackBytes);write32(thread->tib+8,thread->stack);
        write32(thread->tib+0x18,thread->tib);write32(thread->tib+0x20,1);write32(thread->tib+0x24,thread->id);
        write32(thread->tib+0x30,read32(kTibBase+0x30));
        uint32_t handle=threadHandle(*thread);
        if(idOut)write32(idOut,thread->id);
        GuestThread& created=*thread;guestThreads.push_back(std::move(thread));
        if(flags&4)created.suspended=true;else launchThread(created);
        __android_log_print(ANDROID_LOG_INFO,"AMRTS","CreateThread %u start %08x suspended %d from %08x",created.id,start,int(created.suspended),g_cur_func);
        ret(handle,6);return true;
    }
    if(name=="ResumeThread"){
        auto* thread=threadByHandle(arg(0));if(!thread){kernelError(6);ret(UINT32_MAX,1);return true;}
        uint32_t previous=thread->suspended?1:0;
        if(thread->suspended){thread->suspended=false;if(!thread->started)launchThread(*thread);}
        ret(previous,1);return true;
    }
    if(name=="SuspendThread"){
        // Only used for diagnostics (stack sampling); report "not suspended".
        ret(threadByHandle(arg(0))?0:UINT32_MAX,1);return true;
    }
    if(name=="GetThreadContext"){
        uint32_t context=arg(1);if(context)std::memset(static_cast<uint8_t*>(kernelPtr(context,0x2cc,true))+4,0,0x2c8);
        ret(1,2);return true;
    }
    if(name=="GetExitCodeThread"){
        auto* thread=threadByHandle(arg(0));if(!thread){kernelError(6);ret(0,2);return true;}
        write32(arg(1),thread->finished?thread->exitCode:259);ret(1,2);return true;
    }
    return false;
}
