#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <algorithm>
#include <cmath>

struct AndroidGuestInput {
    enum Kind {Pointer,Key,Wheel} kind=Pointer;
    float x=0,y=0;
    uint32_t code=0;
    bool down=false;
};
struct GuestRunControl {
    std::atomic<bool> stopped{false};
    std::atomic<uint32_t> frames{0};
    std::mutex mutex;
    std::deque<AndroidGuestInput> pending;
    void pointer(float x,float y,uint32_t button,bool down){
        if(!std::isfinite(x)||!std::isfinite(y)||button>3)return;
        push({AndroidGuestInput::Pointer,std::clamp(x,0.0f,1.0f),std::clamp(y,0.0f,1.0f),button,down});
    }
    void key(uint32_t scan,bool down){if(scan<256)push({AndroidGuestInput::Key,0,0,scan,down});}
    void wheel(int32_t steps){steps=std::clamp(steps,-3,3);if(steps)push({AndroidGuestInput::Wheel,0,0,uint32_t(steps*120),false});}
private:
    void push(AndroidGuestInput value){
        std::lock_guard<std::mutex> lock(mutex);
        if(stopped.load())return;
        // Coalesce pointer motion without discarding key/button transitions.
        if(value.kind==AndroidGuestInput::Pointer&&value.code==3&&!pending.empty()&&pending.back().kind==AndroidGuestInput::Pointer&&pending.back().code==3)pending.back()=value;
        else pending.push_back(value);
        if(pending.size()>4096){stopped.store(true);pending.clear();}
    }
};
