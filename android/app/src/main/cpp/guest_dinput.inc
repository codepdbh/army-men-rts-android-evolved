// DirectInput 7 ANSI ABI, backed by Android's virtual mouse/keyboard.
constexpr DxGuid inputIid{0x9a4cb684,0x236d,0x11d3,{0x8e,0x9d,0,0xc0,0x4f,0x68,0x44,0xae}};
constexpr DxGuid inputDeviceIid{0x57d7c6bc,0x2356,0x11d3,{0x8e,0x9d,0,0xc0,0x4f,0x68,0x44,0xae}};
constexpr DxGuid mouseGuid{0x6f1d2b60,0xd5a0,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0,0}};
constexpr DxGuid keyboardGuid{0x6f1d2b61,0xd5a0,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0,0}};
struct InputFormatEntry {uint32_t offset,type;};
struct InputDataEvent {uint32_t offset,data,time,sequence;bool axis=false;};
struct InputObject {uint32_t type=0,references=1,dataBytes=0,bufferSize=0,cooperative=0;bool acquired=false,overflow=false;int32_t deltaX=0,deltaY=0,deltaZ=0;std::vector<InputFormatEntry> format;std::deque<InputDataEvent> events;};
std::map<uint32_t,InputObject> inputObjects;
uint32_t inputTables[2]{},inputToken=0,inputSequence=0;
double motionRemainder[2]{};
std::array<uint8_t,256> inputKeyboard{};std::array<uint8_t,3> inputButtons{};
void inputMethod();
void resetInput(){inputObjects.clear();motionRemainder[0]=motionRemainder[1]=0;inputTables[0]=inputTables[1]=inputToken=inputSequence=0;inputKeyboard.fill(0);inputButtons.fill(0);}
uint32_t createInputObject(uint32_t type){
    uint32_t tableIndex=type?1:0,count=type?29:10;
    if(!inputTables[tableIndex]){inputTables[tableIndex]=allocate(count*4);for(uint32_t i=0;i<count;++i)write32(inputTables[tableIndex]+i*4,0x76000000+tableIndex*0x10000+i*16);}
    uint32_t object=allocate(4);write32(object,inputTables[tableIndex]);inputObjects.emplace(object,InputObject{type});return object;
}
recomp_func_t inputLookup(uint32_t address){
    if(address<0x76000000||address>=0x76020000||(address&15))return nullptr;
    uint32_t index=(address&0xffff)/16;if(index>=((address&0x10000)?29u:10u))return nullptr;
    inputToken=address;return inputMethod;
}
void inputDescription(uint32_t output,uint32_t type){
    uint32_t size=read32(output);if(size!=580)throw GuestStop("Unsupported DirectInput instance size");
    std::memset(kernelPtr(output,size,true),0,size);write32(output,size);
    const auto& guid=type==2?mouseGuid:keyboardGuid;
    std::memcpy(kernelPtr(output+4,16,true),&guid,16);std::memcpy(kernelPtr(output+20,16,true),&guid,16);
    write32(output+36,type);const char* name=type==2?"Android mouse":"Android keyboard";
    std::memcpy(kernelPtr(output+40,std::strlen(name)+1,true),name,std::strlen(name)+1);
    std::memcpy(kernelPtr(output+300,std::strlen(name)+1,true),name,std::strlen(name)+1);
}
bool mouseMotionPending(){
    for(const auto& item:inputObjects){const auto& object=item.second;if(!object.acquired||object.type!=2)continue;
        if(!object.bufferSize&&(object.deltaX||object.deltaY))return true;
        for(const auto& event:object.events)if(event.axis)return true;}
    return false;
}
// The engine moves its cursor by (counts*gain)>>16 (16.16 gain, arithmetic
// shift). Pick the count whose integrated result lands closest to `pixels`.
int32_t engineCounts(int32_t pixels,uint32_t gain){
    if(!pixels||!gain)return 0;
    int64_t base=int64_t(std::floor(double(pixels)*65536/gain)),best=base;int64_t bestError=INT64_MAX;
    for(int64_t counts=base-1;counts<=base+2;++counts){
        int64_t error=std::llabs(((counts*int64_t(gain))>>16)-pixels);
        if(error<bestError){bestError=error;best=counts;}
    }
    return int32_t(best);
}
void pumpAndroidInput(){
    if(!runControl)return;std::deque<AndroidGuestInput> pending;
    {std::lock_guard<std::mutex> lock(runControl->mutex);pending.swap(runControl->pending);}
    // Fullscreen Army Men integrates relative mouse counts with width/640 and
    // height/480 gains, and recenters its cursor when a mode is applied.
    // Touch supplies absolute positions: translate from the engine's current
    // cursor, undo those gains, and keep successive queued events contiguous.
    bool fullscreenTouch=read32(0x72252c)&& (read32(0x6bd4d0)&0x1000)&&
        read32(0x6bd124)==uint32_t(displayWidth)&&read32(0x6bd128)==uint32_t(displayHeight);
    uint32_t gainX=fullscreenTouch?read32(0x722308):65536,gainY=fullscreenTouch?read32(0x72230c):65536;
    // Resynchronise only once the engine has integrated every earlier delta;
    // re-reading a stale cursor would apply the same motion twice.
    if(fullscreenTouch&&!pending.empty()&&!mouseMotionPending()){cursorX=int32_t(read32(0x7222f0));cursorY=int32_t(read32(0x7222f4));}
    for(const auto& event:pending){
        uint32_t time=messageTime(),sequence=++inputSequence;
        int32_t dx=0,dy=0,dz=0;
        if(event.kind==AndroidGuestInput::Pointer){
            int32_t x=int32_t(event.x*std::max(displayWidth-1,0)),y=int32_t(event.y*std::max(displayHeight-1,0));
            dx=x-cursorX;dy=y-cursorY;cursorX=x;cursorY=y;
            if(fullscreenTouch&&gainX&&gainY){dx=engineCounts(dx,gainX);dy=engineCounts(dy,gainY);}
            if(event.code<3)inputButtons[event.code]=event.down?128:0;
        }else if(event.kind==AndroidGuestInput::Motion){
            // The engine recentres its cursor while the right button pans, so
            // camera drags must arrive as deltas rather than absolute targets.
            // Carry sub-count remainders so slow drags are not lost to rounding.
            double countsX=event.x*displayWidth*(fullscreenTouch&&gainX?65536.0/gainX:1)+motionRemainder[0];
            double countsY=event.y*displayHeight*(fullscreenTouch&&gainY?65536.0/gainY:1)+motionRemainder[1];
            dx=int32_t(std::trunc(countsX));dy=int32_t(std::trunc(countsY));motionRemainder[0]=countsX-dx;motionRemainder[1]=countsY-dy;
            cursorX=std::clamp(cursorX+int32_t(std::lround(event.x*displayWidth)),0,std::max(displayWidth-1,0));
            cursorY=std::clamp(cursorY+int32_t(std::lround(event.y*displayHeight)),0,std::max(displayHeight-1,0));
        }else if(event.kind==AndroidGuestInput::Wheel)dz=int32_t(event.code);else inputKeyboard[event.code]=event.down?128:0;
        for(auto& item:inputObjects){auto& object=item.second;if(!object.acquired)continue;
            bool mouse=event.kind!=AndroidGuestInput::Key;
            if((mouse&&object.type!=2)||(!mouse&&object.type!=3))continue;
            object.deltaX+=dx;object.deltaY+=dy;object.deltaZ+=dz;
            for(const auto& entry:object.format){
                uint32_t type=entry.type&255,instance=(entry.type>>8)&65535,value=0;bool changed=false;
                if(mouse&&type&3){if(instance==0&&dx){value=uint32_t(dx);changed=true;}if(instance==1&&dy){value=uint32_t(dy);changed=true;}if(instance==2&&dz){value=uint32_t(dz);changed=true;}}
                else if(type&12){if(mouse&&event.kind==AndroidGuestInput::Pointer&&event.code<3&&instance==event.code){value=event.down?128:0;changed=true;}
                    if(!mouse&&instance==event.code){value=event.down?128:0;changed=true;}}
                if(changed&&object.bufferSize){if(object.events.size()>=object.bufferSize){object.events.pop_front();object.overflow=true;}object.events.push_back({entry.offset,value,time,sequence,mouse&&(type&3)&&instance<2});}
            }
        }
    }
}
void inputMethod(){
    uint32_t index=(inputToken&0xffff)/16;auto found=inputObjects.find(arg(0));
    if(found==inputObjects.end())throw GuestStop("Invalid DirectInput object");auto& object=found->second;
    if(runControl&&object.type==2&&(index==9||index==10)&&object.acquired)++runControl->inputPolls;
    if(traceCalls&&index!=9&&index!=10&&index!=25)__android_log_print(ANDROID_LOG_DEBUG,"AMRTS","Input type=%u method=%u caller=%08x",object.type,index,g_cur_func);
    if(index==0){bool supported=guidEquals(arg(1),iidUnknown)||guidEquals(arg(1),object.type?inputDeviceIid:inputIid);write32(arg(2),supported?arg(0):0);if(supported)++object.references;ret(supported?0:0x80004002u,3);return;}
    if(index==1){ret(++object.references,1);return;}
    if(index==2){uint32_t count=--object.references;if(!count){uint32_t address=arg(0);inputObjects.erase(address);heaps->free(GuestHeaps::process,address);}ret(count,1);return;}
    if(!object.type){
        if(index==3||index==9){uint32_t type=guidEquals(arg(1),mouseGuid)?2:guidEquals(arg(1),keyboardGuid)?3:0;
            if(!type){ret(0x80070002u,index==9?5:4);return;}
            write32(arg(index==9?3:2),createInputObject(type));ret(0,index==9?5:4);return;}
        if(index==4){uint32_t type=arg(1),callback=arg(2),context=arg(3),description=allocate(580);
            for(uint32_t candidate:{2u,3u})if(!type||type==candidate){write32(description,580);inputDescription(description,candidate);if(!callGuest(callback,{description,context}))break;}
            heaps->free(GuestHeaps::process,description);ret(0,5);return;}
        if(index==5){ret(guidEquals(arg(1),mouseGuid)||guidEquals(arg(1),keyboardGuid)?0:0x80070002u,2);return;}
    }else{
        if(index==4){
            uint32_t callback=arg(1),context=arg(2),flags=arg(3),output=allocate(316);
            uint32_t count=object.type==2?6:256;
            for(uint32_t i=0;i<count;++i){
                uint32_t type=object.type==2&&i<3?1:4,instance=object.type==2&&i>=3?i-3:i;
                if(flags&&!(flags&type))continue;
                std::memset(kernelPtr(output,316,true),0,316);write32(output,316);
                DxGuid guid=keyboardGuid;
                guid.a=object.type==3?0x55728220:(type==1?0xa36d02e0+i:0xa36d02f0);
                if(object.type==3){guid.b=0xd33c;}else guid.b=0xc9f3;
                std::memcpy(kernelPtr(output+4,16,true),&guid,16);
                write32(output+20,object.type==3?i:i<3?i*4:12+i-3);write32(output+24,type|(instance<<8));
                std::string label=(object.type==3?"Key ":type==1?"Axis ":"Button ")+std::to_string(instance);
                std::memcpy(kernelPtr(output+32,label.size()+1,true),label.c_str(),label.size()+1);
                if(!callGuest(callback,{output,context}))break;
            }
            heaps->free(GuestHeaps::process,output);ret(0,4);return;
        }
        if(index==3){uint32_t size=read32(arg(1));if(size!=24&&size!=44){ret(0x80070057u,2);return;}
            std::memset(kernelPtr(arg(1),size,true),0,size);write32(arg(1),size);write32(arg(1)+4,1);write32(arg(1)+8,object.type);
            write32(arg(1)+12,object.type==2?3:0);write32(arg(1)+16,object.type==2?3:256);ret(0,2);return;}
        if(index==6){uint32_t property=arg(1),header=arg(2);if(read32(header)<20||read32(header+4)!=16){ret(0x80070057u,3);return;}
            if(property==1){object.bufferSize=std::min(read32(header+16),4096u);object.events.clear();object.overflow=false;}else if(property!=2&&property!=4)throw GuestStop("DirectInput property pending: "+std::to_string(property));ret(0,3);return;}
        if(index==7||index==8){object.acquired=index==7;if(index==8){object.events.clear();object.deltaX=object.deltaY=object.deltaZ=0;}ret(0,1);return;}
        if(index==11){uint32_t format=arg(1);if(read32(format)!=24||read32(format+4)!=16){ret(0x80070057u,2);return;}
            object.dataBytes=read32(format+12);if(object.dataBytes>256){ret(0x80070057u,2);return;}
            uint32_t count=read32(format+16),entries=read32(format+20);if(count>1024){ret(0x80070057u,2);return;}
            kernelPtr(entries,uint64_t(count)*16);object.format.clear();uint32_t nextAxis=0,nextButton=0;
            for(uint32_t i=0;i<count;++i){InputFormatEntry entry{read32(entries+i*16+4),read32(entries+i*16+8)};
                if(((entry.type>>8)&65535)==65535){
                    uint32_t instance=0;
                    if(entry.type&3){uint32_t guid=read32(entries+i*16);uint32_t axis=guid?read32(guid):0;
                        instance=axis>=0xa36d02e0&&axis<=0xa36d02e2?axis-0xa36d02e0:nextAxis++;}
                    else instance=nextButton++;
                    entry.type=(entry.type&0xff0000ffu)|(instance<<8);
                }
                uint32_t bytes=(entry.type&3)?4:1;if(uint64_t(entry.offset)+bytes>object.dataBytes){ret(0x80070057u,2);return;}
                object.format.push_back(entry);}
            ret(0,2);return;}
        if(index==12){ret(0,2);return;}
        if(index==13){object.cooperative=arg(2);ret(0,3);return;}
        if(index==15){inputDescription(arg(1),object.type);ret(0,2);return;}
        if(index==25){ret(object.acquired?0:0x8007000cu,1);return;}
        if(index==9){if(!object.acquired){ret(0x8007000cu,3);return;}if(arg(1)!=object.dataBytes){ret(0x80070057u,3);return;}
            auto* out=static_cast<uint8_t*>(kernelPtr(arg(2),object.dataBytes,true));std::memset(out,0,object.dataBytes);
            for(const auto& entry:object.format){uint32_t type=entry.type&255,instance=(entry.type>>8)&65535;
                if(object.type==2&&type&3){int32_t value=instance==0?object.deltaX:instance==1?object.deltaY:instance==2?object.deltaZ:0;std::memcpy(out+entry.offset,&value,4);}
                else if(type&12)out[entry.offset]=object.type==3&&instance<256?inputKeyboard[instance]:object.type==2&&instance<3?inputButtons[instance]:0;
            }
            object.deltaX=object.deltaY=object.deltaZ=0;ret(0,3);return;}
        if(index==10){
            if(!object.acquired){ret(0x8007000cu,5);return;}uint32_t size=arg(1),output=arg(2),requested=read32(arg(3)),flags=arg(4);
            if((size!=16&&size!=20)||flags&~1u){ret(0x80070057u,5);return;}
            uint32_t count=std::min(requested,uint32_t(object.events.size()));
            if(output){kernelPtr(output,uint64_t(count)*size,true);for(uint32_t i=0;i<count;++i){std::memset(kernelPtr(output+i*size,size,true),0,size);std::memcpy(kernelPtr(output+i*size,16,true),&object.events[i],16);}}
            if(!(flags&1)){for(uint32_t i=0;i<count;++i)object.events.pop_front();}
            write32(arg(3),count);bool overflow=object.overflow;if(!(flags&1))object.overflow=false;ret(overflow?1:0,5);return;
        }
    }
    throw GuestStop("DirectInput method pending: "+std::to_string(index)+" from 0x"+hex(g_cur_func));
}
uint32_t scanToVirtual(uint32_t scan){
    static const char digits[]="1234567890",top[]="QWERTYUIOP",middle[]="ASDFGHJKL",bottom[]="ZXCVBNM";
    if(scan>=2&&scan<=11)return digits[scan-2];if(scan>=16&&scan<=25)return top[scan-16];
    if(scan>=30&&scan<=38)return middle[scan-30];if(scan>=44&&scan<=50)return bottom[scan-44];
    switch(scan){case 1:return 27;case 14:return 8;case 15:return 9;case 28:case 156:return 13;
    case 29:case 157:return 17;case 42:case 54:return 16;case 56:case 184:return 18;case 57:return 32;
    case 58:return 20;case 203:return 37;case 200:return 38;case 205:return 39;case 208:return 40;
    case 12:return 189;case 13:return 187;case 39:return 186;case 40:return 222;case 41:return 192;
    case 51:return 188;case 52:return 190;case 53:return 191;case 26:return 219;case 27:return 221;case 43:return 220;}
    if(scan>=59&&scan<=68)return 112+scan-59;if(scan==87)return 122;if(scan==88)return 123;return 0;
}
uint8_t virtualKeyState(uint32_t key){
    if(key==1)return inputButtons[0];if(key==2)return inputButtons[1];if(key==4)return inputButtons[2];
    uint8_t value=0;for(uint32_t scan=0;scan<256;++scan)if(scanToVirtual(scan)==key)value|=inputKeyboard[scan];return value;
}
// Character of a virtual key on a US layout (ToAscii and TranslateMessage).
uint16_t virtualToChar(uint32_t key,bool shift,bool caps){
    if(key>='A'&&key<='Z')return (shift!=caps)?key:key+32;
    if(key>='0'&&key<='9'){const char shifted[]=")!@#$%^&*(";return shift?shifted[key-'0']:key;}
    if(key==32||key==13||key==9||key==8||key==27)return key;
    const char* plain=nullptr;const char* upper=nullptr;
    switch(key){case 186:plain=";";upper=":";break;case 187:plain="=";upper="+";break;case 188:plain=",";upper="<";break;
        case 189:plain="-";upper="_";break;case 190:plain=".";upper=">";break;case 191:plain="/";upper="?";break;
        case 192:plain="`";upper="~";break;case 219:plain="[";upper="{";break;case 220:plain="\\";upper="|";break;
        case 221:plain="]";upper="}";break;case 222:plain="'";upper="\"";break;}
    return plain?uint16_t(uint8_t((shift?upper:plain)[0])):0;
}
bool inputDispatch(const std::string& name){
    if(name=="GetKeyboardState"){
        auto* output=static_cast<uint8_t*>(kernelPtr(arg(0),256,true));for(uint32_t key=0;key<256;++key)output[key]=virtualKeyState(key);ret(1,1);return true;
    }
    if(name=="GetKeyState"||name=="GetAsyncKeyState"){ret(virtualKeyState(arg(0))&128?0xffff8000u:0,1);return true;}
    if(name=="MapVirtualKeyA"){
        uint32_t key=arg(0),mode=arg(1),value=0;
        if(mode==1)value=scanToVirtual(key);else if(mode==0){for(uint32_t scan=0;scan<256;++scan)if(scanToVirtual(scan)==key){value=scan;break;}}
        else if(mode==2)value=(key>=32&&key<127)?key:0;ret(value,2);return true;
    }
    if(name=="ToAscii"){
        uint32_t key=arg(0),state=arg(2);auto* keys=static_cast<const uint8_t*>(kernelPtr(state,256));uint16_t character=0;
        character=virtualToChar(key,keys[16]&128,keys[20]&1);
        if(character)std::memcpy(kernelPtr(arg(3),2,true),&character,2);ret(character?1:0,5);return true;
    }
    if(name!="DirectInputCreateEx")return false;
    if(arg(4)){ret(0x80040110u,5);return true;}
    if(arg(1)!=0x700||!guidEquals(arg(2),inputIid)){ret(0x80004002u,5);return true;}
    write32(arg(3),createInputObject(0));ret(0,5);return true;
}
