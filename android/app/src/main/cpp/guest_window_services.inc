constexpr uint32_t desktopWindow = 0xf0000300;
int32_t displayWidth = 1280, displayHeight = 720, cursorX = 0, cursorY = 0;
void resetWindows() {
    windowClasses.clear(); windows.clear(); paintContexts.clear();
    std::lock_guard<std::mutex> lock(messageMutex); windowMessages.clear();
    nextClassAtom=1;nextWindow=0xf0000301;nextPaintContext=0x74010000;
    currentCursor=activeWindow=0;cursorVisibility=cursorX=cursorY=0;
}
void findWindow() {
    uint32_t classId = arg(0); std::string className;
    if (classId > 0xffff) className = lower(guestString(classId));
    else if (classId) for (const auto& cls : windowClasses) if (cls.second.atom == classId) className = lower(cls.second.name);
    std::string title = arg(1) ? lower(guestString(arg(1))) : std::string{};
    for (const auto& window : windows) {
        if ((classId && lower(window.second.className) != className) || (arg(1) && lower(window.second.title) != title)) continue;
        ret(window.first, 2); return;
    }
    ret(0, 2);
}
void windowRect() {
    int32_t rectangle[] = {0,0,displayWidth,displayHeight};
    if (arg(0) != desktopWindow) {
        auto window=windows.find(arg(0));if(window==windows.end()){lastError=1400;ret(0,2);return;}
        auto& value=window->second;rectangle[0]=value.x;rectangle[1]=value.y;rectangle[2]=value.x+value.width;rectangle[3]=value.y+value.height;
    }
    std::memcpy(kernelPtr(arg(1),16,true),rectangle,16);ret(1,2);
}
void clientRect() {
    auto window=windows.find(arg(0));if(window==windows.end()){lastError=1400;ret(0,2);return;}
    int32_t rectangle[]={0,0,window->second.width,window->second.height};
    std::memcpy(kernelPtr(arg(1),16,true),rectangle,16);ret(1,2);
}
void windowMetrics() {
    uint32_t index=arg(0),value=0;
    if(index==0||index==16||index==59||index==61||index==78)value=displayWidth;
    else if(index==1||index==17||index==60||index==62||index==79)value=displayHeight;
    else if(index==80)value=1;
    else if(index==19||index==75)value=1;
    else if(index==43)value=3;
    else if(index==13||index==14)value=32;
    else if(index==36||index==37)value=4;
    else if(index>90)throw GuestStop("GetSystemMetrics index pending: "+std::to_string(index));
    ret(value,1);
}
void windowMessageBox() {
    std::string text=arg(1)?guestString(arg(1)):std::string{},caption=arg(2)?guestString(arg(2)):std::string{};
    __android_log_print(ANDROID_LOG_WARN,"AMRTS","Guest message [%s]: %s",caption.c_str(),text.c_str());
    uint32_t answers[]={1,1,5,6,6,2,11};uint32_t type=arg(3)&15;ret(type<7?answers[type]:1,4);
}
const std::map<std::string,recomp_func_t> windowFunctions={
    {"FindWindowA",findWindow},{"RegisterClassA",registerClass},{"RegisterClassExA",registerClassEx},{"CreateWindowExA",createWindowEx},
    {"DefWindowProcA",defaultWindowProc},{"GetWindowLongA",getWindowLong},{"SetWindowLongA",setWindowLong},
    {"AdjustWindowRect",adjustWindowRect},{"ShowWindow",showWindow},{"SetWindowPos",setWindowPos},{"UpdateWindow",updateWindow},
    {"GetWindowRect",windowRect},{"GetClientRect",clientRect},{"GetSystemMetrics",windowMetrics},{"MessageBoxA",windowMessageBox},
    {"PeekMessageA",peekMessage},{"GetMessageA",getMessage},{"TranslateMessage",translateMessage},{"DispatchMessageA",dispatchMessage},
    {"PostMessageA",postMessage},{"PostQuitMessage",postQuitMessage},{"SendMessageA",sendMessage},
    {"SetCursor",setCursor},{"ShowCursor",showCursor},{"GetActiveWindow",getActiveWindow},{"GetFocus",getActiveWindow},
    {"SetActiveWindow",setActiveWindow},{"SetFocus",setActiveWindow},{"SetForegroundWindow",setForegroundWindow},
    {"BeginPaint",beginPaint},{"EndPaint",endPaint},{"wsprintfA",winSprintfA},{"wvsprintfA",winFormatA}
};
bool windowDispatch(const std::string& name) {
    if(name=="SystemParametersInfoA"){
        uint32_t action=arg(0),out=arg(2);
        if(action==0x16){write32(out,1);ret(1,4);return true;}
        if(action==0xa){write32(out,31);ret(1,4);return true;}
        if(action==3){write32(out,6);write32(out+4,10);write32(out+8,1);ret(1,4);return true;}
        if(action==48){int32_t rectangle[]={0,0,displayWidth,displayHeight};std::memcpy(kernelPtr(out,16,true),rectangle,16);ret(1,4);return true;}
        throw GuestStop("SystemParametersInfo action pending: "+std::to_string(action));
    }
    if(name=="RedrawWindow"){
        uint32_t handle=arg(0),flags=arg(3);
        if(!windows.count(handle)){lastError=1400;ret(0,4);return true;}
        if(arg(2))throw GuestStop("RedrawWindow region support pending");
        if(flags&1){
            if(flags&0x100){if(flags&4)windowMessage(handle,0x14,0,0);windowMessage(handle,0xf,0,0);}
            else {std::lock_guard<std::mutex> lock(messageMutex);windowMessages.push_back({handle,0xf,0,0,messageTime()});}
        }
        ret(1,4);return true;
    }
    auto found=windowFunctions.find(name);if(found!=windowFunctions.end()){found->second();return true;}
    if(name=="GetStockObject") {ret(arg(0)<20?0x74000000+arg(0):0,1);return true;}
    if(name=="DeleteObject") {lastError=6;ret(0,1);return true;}
    if(name=="LoadIconA") {lastError=1813;ret(0,2);return true;}
    if(name=="LoadCursorA") {ret(arg(1)<=0xffff?0x74020000+arg(1):0,2);return true;}
    if(name=="GetDesktopWindow") {ret(desktopWindow,0);return true;}
    if(name=="GetDoubleClickTime") {ret(500,0);return true;}
    if(name=="GetMenu"&&windows.count(arg(0))) {ret(0,1);return true;}
    if(name=="SetMenu"&&windows.count(arg(0))&&!arg(1)) {ret(1,2);return true;}
    if(name=="DrawMenuBar"&&windows.count(arg(0))) {ret(1,1);return true;}
    if(name=="GetCursorPos") {int32_t point[]={cursorX,cursorY};std::memcpy(kernelPtr(arg(0),8,true),point,8);ret(1,1);return true;}
    if(name=="SetCursorPos") {cursorX=arg(0);cursorY=arg(1);ret(1,2);return true;}
    if(name=="SetRect") {uint32_t rectangle[]={arg(1),arg(2),arg(3),arg(4)};std::memcpy(kernelPtr(arg(0),16,true),rectangle,16);ret(1,5);return true;}
    if(name=="ScreenToClient"||name=="ClientToScreen") {
        auto window=windows.find(arg(0));if(window==windows.end()){lastError=1400;ret(0,2);return true;}
        uint32_t point=arg(1);kernelPtr(point,8,true);int32_t sign=name=="ScreenToClient"?-1:1;
        MEM32(point)+=sign*window->second.x;MEM32(point+4)+=sign*window->second.y;ret(1,2);return true;
    }
    return false;
}
