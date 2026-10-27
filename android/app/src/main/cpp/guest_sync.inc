enum class SyncKind { Mutex, Event, Semaphore };
struct SyncObject {
    SyncKind kind;
    bool manual = false, signaled = false;
    uint32_t owner = 0, depth = 0, count = 0, maximum = 0;
};
std::unordered_map<uint32_t, std::shared_ptr<SyncObject>> syncHandles;
std::unordered_map<std::string, std::weak_ptr<SyncObject>> namedSync;
uint32_t nextSyncHandle = 0x72000000;
void resetSync() { syncHandles.clear(); namedSync.clear(); nextSyncHandle = 0x72000000; }
bool syncReady(const SyncObject& object) {
    if (object.kind == SyncKind::Mutex) return !object.owner || object.owner == currentThreadId();
    if (object.kind == SyncKind::Event) return object.signaled;
    return object.count > 0;
}
void syncAcquire(SyncObject& object) {
    if (object.kind == SyncKind::Mutex) { object.owner = currentThreadId(); ++object.depth; }
    else if (object.kind == SyncKind::Event) { if (!object.manual) object.signaled = false; }
    else --object.count;
}

bool syncDispatch(const std::string& name) {
    if (name == "CreateMutexA" || name == "CreateEventA" || name == "CreateSemaphoreA") {
        SyncKind kind = name == "CreateMutexA" ? SyncKind::Mutex : name == "CreateEventA" ? SyncKind::Event : SyncKind::Semaphore;
        uint32_t nameAddress = arg(kind == SyncKind::Mutex ? 2 : 3);
        std::string objectName = nameAddress ? guestString(nameAddress) : std::string{};
        std::shared_ptr<SyncObject> object;
        if (!objectName.empty()) {
            auto found = namedSync.find(objectName);
            if (found != namedSync.end()) object = found->second.lock();
        }
        if (object) {
            if (object->kind != kind) { kernelError(6); ret(0, kind == SyncKind::Mutex ? 3 : 4); return true; }
            kernelError(183);
        } else {
            if (kind == SyncKind::Semaphore && (!arg(2) || arg(2) > INT32_MAX || arg(1) > arg(2))) {
                kernelError(87); ret(0, 4); return true;
            }
            object = std::make_shared<SyncObject>(); object->kind = kind;
            if (kind == SyncKind::Mutex && arg(1)) { object->owner = currentThreadId(); object->depth = 1; }
            if (kind == SyncKind::Event) { object->manual = arg(1) != 0; object->signaled = arg(2) != 0; }
            if (kind == SyncKind::Semaphore) { object->count = arg(1); object->maximum = arg(2); }
            if (!objectName.empty()) namedSync[objectName] = object;
            kernelError(0);
        }
        uint32_t handle = nextSyncHandle; nextSyncHandle += 4;
        syncHandles.emplace(handle, object); ret(handle, kind == SyncKind::Mutex ? 3 : 4); return true;
    }
    if (name == "ReleaseMutex" || name == "SetEvent" || name == "ResetEvent" || name == "PulseEvent" || name == "ReleaseSemaphore") {
        uint32_t count = name == "ReleaseSemaphore" ? 3 : 1;
        auto found = syncHandles.find(arg(0));
        if (found == syncHandles.end()) { kernelError(6); ret(0, count); return true; }
        auto& object = *found->second;
        if (name == "ReleaseMutex" && object.kind == SyncKind::Mutex) {
            if (object.owner != currentThreadId() || !object.depth) { kernelError(288); ret(0, 1); }
            else { if (!--object.depth) object.owner = 0; ret(1, 1); }
        } else if (name == "ReleaseSemaphore" && object.kind == SyncKind::Semaphore) {
            if (!arg(1) || arg(1) > object.maximum - object.count) { kernelError(298); ret(0, 3); }
            else { if (arg(2)) write32(arg(2), object.count); object.count += arg(1); ret(1, 3); }
        } else if (object.kind == SyncKind::Event && name != "ReleaseMutex" && name != "ReleaseSemaphore") {
            object.signaled = name == "SetEvent"; ret(1, 1);
        } else { kernelError(6); ret(0, count); }
        return true;
    }
    if (name == "WaitForSingleObject" || name == "WaitForMultipleObjects") {
        bool multiple = name == "WaitForMultipleObjects";
        uint32_t count = multiple ? arg(0) : 1, timeout = multiple ? arg(3) : arg(1);
        bool all = multiple && arg(2);
        if (!count || count > 64) { kernelError(87); ret(UINT32_MAX, multiple ? 4 : 2); return true; }
        std::vector<std::shared_ptr<SyncObject>> objects;
        if (multiple) kernelPtr(arg(1), uint64_t(count) * 4);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t handle = multiple ? MEM32(arg(1) + i * 4) : arg(0);
            auto found = syncHandles.find(handle);
            if (found == syncHandles.end()) { kernelError(6); ret(UINT32_MAX, multiple ? 4 : 2); return true; }
            if (std::find(objects.begin(), objects.end(), found->second) != objects.end()) {
                kernelError(87); ret(UINT32_MAX, multiple ? 4 : 2); return true;
            }
            objects.push_back(found->second);
        }
        auto waitEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout == UINT32_MAX ? 0 : timeout);
        for (;;) {
            bool ready = all;
            uint32_t index = 0;
            if (all) { for (const auto& object : objects) ready &= syncReady(*object); }
            else { for (index = 0; index < count; ++index) if (syncReady(*objects[index])) { ready = true; break; } }
            if (ready) {
                if (all) for (const auto& object : objects) syncAcquire(*object);
                else syncAcquire(*objects[index]);
                ret(all ? 0 : index, multiple ? 4 : 2); return true;
            }
            if (!timeout || (timeout != UINT32_MAX && std::chrono::steady_clock::now() >= waitEnd)) {
                ret(258, multiple ? 4 : 2); return true;
            }
            // Another guest thread must signal: hand it the machine.
            if (timeout == UINT32_MAX && !otherThreadsRunnable() && !runControl)
                throw GuestStop("Blocking wait with no other guest thread");
            progressCheck(); machineYield(1);
        }
    }
    if (name == "CloseHandle") {
        if (!syncHandles.erase(arg(0))) { kernelError(6); ret(0, 1); }
        else ret(1, 1);
        return true;
    }
    if (name == "DuplicateHandle") {
        auto found = syncHandles.find(arg(1));
        if (arg(0) != UINT32_MAX || arg(2) != UINT32_MAX || found == syncHandles.end()) {
            kernelError(6); ret(0, 7); return true;
        }
        uint32_t handle = nextSyncHandle; nextSyncHandle += 4;
        syncHandles.emplace(handle, found->second); write32(arg(3), handle);
        if (arg(6) & 1) syncHandles.erase(arg(1)); ret(1, 7); return true;
    }
    return false;
}
