#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cmath>

struct AndroidGuestInput {
    // Motion: relative displacement in normalised screen units (camera drags).
    enum Kind {Pointer,Key,Wheel,Motion} kind=Pointer;
    float x=0,y=0;
    uint32_t code=0;
    bool down=false;
};
struct ANativeWindow;
struct GuestRunControl {
    std::atomic<bool> stopped{false};
    std::atomic<uint32_t> frames{0};
    // Mouse reads by the engine (GetDeviceState/GetDeviceData): input pacing.
    std::atomic<uint32_t> inputPolls{0};
    // Kind of the engine cursor shown this frame (see guest_cursor.inc).
    std::atomic<int32_t> cursorKind{0};
    // Game resolution (width << 16 | height), for locating the interface.
    std::atomic<uint32_t> displaySize{0};
    // Port-level Miles master volume 0..127; -1 follows the game's saved setting.
    std::atomic<int32_t> masterVolume{127};
    std::mutex mutex;
    std::deque<AndroidGuestInput> pending;
    // App in background: the game thread parks in progressCheck (clock frozen,
    // audio silent) instead of the session being closed.
    std::atomic<bool> paused{false};
    // Android window hand-over. The UI thread posts a new window (null when the
    // surface is destroyed) and waits until the game thread, which owns the
    // renderer, has swapped it. Both pointers hold a window reference.
    std::mutex windowMutex;
    std::condition_variable windowSwapped;
    std::atomic<bool> windowPending{false};
    ANativeWindow* requestedWindow=nullptr;
    ANativeWindow* shownWindow=nullptr;
    uint64_t windowRequests=0,windowServed=0;
    // Direct camera drag (normalised screen coordinates). `serial` changes on
    // every new gesture; on release the velocity (screen units/s) drives inertia.
    // `began` survives the release so a flick shorter than one game frame is still applied.
    // `zoom` is the finger-spread ratio since BEGIN (two-finger gestures; 1 = unchanged).
    struct CameraDrag {uint32_t serial=0;bool active=false,began=false;float startX=0,startY=0,x=0,y=0,zoom=1,vx=0,vy=0;};
    std::mutex cameraMutex;
    CameraDrag cameraDrag;
    void dragCamera(int phase,float x,float y,float zoom,float vx,float vy){
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(zoom)||zoom<=0||!std::isfinite(vx)||!std::isfinite(vy))return;
        std::lock_guard<std::mutex> lock(cameraMutex);
        if(phase==0){++cameraDrag.serial;cameraDrag.active=cameraDrag.began=true;cameraDrag.vx=cameraDrag.vy=0;cameraDrag.startX=x;cameraDrag.startY=y;}
        if(phase==3){cameraDrag.active=cameraDrag.began=false;cameraDrag.vx=cameraDrag.vy=0;++cameraDrag.serial;return;}
        cameraDrag.x=x;cameraDrag.y=y;cameraDrag.zoom=std::clamp(zoom,0.1f,10.0f);
        if(phase==2){cameraDrag.active=false;cameraDrag.vx=vx;cameraDrag.vy=vy;}
    }
    void pointer(float x,float y,uint32_t button,bool down){
        if(!std::isfinite(x)||!std::isfinite(y)||button>3)return;
        push({AndroidGuestInput::Pointer,std::clamp(x,0.0f,1.0f),std::clamp(y,0.0f,1.0f),button,down});
    }
    void key(uint32_t scan,bool down){if(scan<256)push({AndroidGuestInput::Key,0,0,scan,down});}
    void motion(float dx,float dy){
        if(!std::isfinite(dx)||!std::isfinite(dy)||(dx==0&&dy==0))return;
        std::lock_guard<std::mutex> lock(mutex);if(stopped.load())return;
        if(!pending.empty()&&pending.back().kind==AndroidGuestInput::Motion){pending.back().x+=dx;pending.back().y+=dy;return;}
        pending.push_back({AndroidGuestInput::Motion,std::clamp(dx,-1.0f,1.0f),std::clamp(dy,-1.0f,1.0f),0,false});
    }
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
