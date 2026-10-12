std::map<uint32_t, uint32_t> timerPeriods;
void resetTimers() { timerPeriods.clear(); }
bool timerDispatch(const std::string& name) {
    if (name == "GetLocalTime" || name == "GetSystemTime" || name == "FileTimeToSystemTime") {
        auto now = std::chrono::system_clock::now();
        time_t seconds = std::chrono::system_clock::to_time_t(now);
        uint32_t milliseconds = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
        uint32_t output = arg(0);
        if (name == "FileTimeToSystemTime") {
            uint64_t ticks; std::memcpy(&ticks, kernelPtr(arg(0), 8), 8);
            if (ticks >= 0x8000000000000000ULL) { kernelError(87); ret(0, 2); return true; }
            seconds = time_t(int64_t(ticks / 10000000ULL) - 11644473600LL);
            milliseconds = uint32_t(ticks % 10000000ULL / 10000); output = arg(1);
        }
        std::tm utc{};
        if (!gmtime_r(&seconds, &utc)) { kernelError(87); ret(0, name == "FileTimeToSystemTime" ? 2 : 1); return true; }
        uint16_t fields[] = {uint16_t(utc.tm_year + 1900), uint16_t(utc.tm_mon + 1), uint16_t(utc.tm_wday),
            uint16_t(utc.tm_mday), uint16_t(utc.tm_hour), uint16_t(utc.tm_min), uint16_t(utc.tm_sec), uint16_t(milliseconds)};
        std::memcpy(kernelPtr(output, 16, true), fields, 16);
        ret(name == "FileTimeToSystemTime" ? 1 : 0, name == "FileTimeToSystemTime" ? 2 : 1); return true;
    }
    if (name == "GetTimeZoneInformation") {
        uint32_t out = arg(0); std::memset(kernelPtr(out, 172, true), 0, 172);
        uint16_t utc[] = {'U', 'T', 'C', 0};
        std::memcpy(kernelPtr(out + 4, 8, true), utc, 8); std::memcpy(kernelPtr(out + 88, 8, true), utc, 8);
        ret(0, 1); return true;
    }
    if (name == "FileTimeToLocalFileTime") {
        uint64_t ticks; std::memcpy(&ticks, kernelPtr(arg(0), 8), 8);
        std::memcpy(kernelPtr(arg(1), 8, true), &ticks, 8); ret(1, 2); return true;
    }
    if (name == "timeGetTime" || name == "GetTickCount") {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        ret(uint32_t(ms), 0); return true;
    }
    if (name == "timeGetDevCaps") {
        if (arg(1) < 8 || !memory->writable(arg(0), 8)) ret(97, 2);
        else { write32(arg(0), 1); write32(arg(0) + 4, 1000000); ret(0, 2); }
        return true;
    }
    if (name == "timeBeginPeriod" || name == "timeEndPeriod") {
        uint32_t period = arg(0);
        if (!period || period > 1000000) ret(97, 1);
        else if (name == "timeBeginPeriod") { ++timerPeriods[period]; ret(0, 1); }
        else {
            auto found = timerPeriods.find(period);
            if (found == timerPeriods.end()) ret(97, 1);
            else { if (!--found->second) timerPeriods.erase(found); ret(0, 1); }
        }
        return true;
    }
    if (name == "Sleep") {
        uint32_t ms = arg(0);
        if (ms == UINT32_MAX) throw GuestStop("Infinite Sleep requires guest scheduler");
        while (ms) { progressCheck(); uint32_t chunk = std::min(ms, 10u);
            std::this_thread::sleep_for(std::chrono::milliseconds(chunk)); ms -= chunk; }
        if (!arg(0)) std::this_thread::yield(); ret(0, 1); return true;
    }
    return false;
}
