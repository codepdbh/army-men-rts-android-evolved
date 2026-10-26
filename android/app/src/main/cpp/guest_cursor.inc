// Engine cursor state, read once per presented frame.
//
// The game picks a named cursor for whatever the mouse is over (interface,
// world, valid/invalid build site, attack target, ...). CursorSys::Set
// (0x004A3F00) stores the active cursor of each layer at 0x00727758 + layer*4;
// cursors live in a tree at 0x00727768 keyed by the CRC of their name
// (node: crc, cursor, left, right). The CRC is the engine's table CRC
// (0x004D9A30): case table at 0x0069C038, CRC table at 0x0069BC38.
// Touch uses this to know the game's mode and what a tap would do.
constexpr uint32_t cursorCurrent=0x00727758,cursorTree=0x00727768,cursorCaseTable=0x0069C038,cursorCrcTable=0x0069BC38;
const int cursorDebug=[]{char value[PROP_VALUE_MAX]{};__system_property_get("debug.amrts.cursor",value);return std::atoi(value);}();
struct CursorName {const char* name;int kind;};
// Kinds shared with GameProbeActivity (GameState.CURSOR_*).
const CursorName cursorNames[]={
    {"Default",1},{"ClientSelect",1},{"ClientNone",1},{"IBeam",1},{"ClientJumpScroll",1},
    {"ClientMove",2},{"ClientTurn",2},{"ClientAttack",3},{"ClientAttack::Target",3},{"ClientMoveAttack",3},
    {"ClientConstruct",4},{"ClientNoConstruct",5},
    {"ClientNoMove",6},{"ClientNoAttack",6},{"ClientNoAttack::Target",6},{"ClientNoSetRally",6},{"ClientNoUnloadCargo",6},{"ClientNoUseOffMap",6},{"ClientNoUseOffMap::Target",6},{"ClientWallNoActivate",6},
    {"ClientCollect",7},{"ClientStore",7},{"ClientSetRally",8},
    {"ClientBoard",9},{"ClientTransport",9},{"ClientUnloadCargo",9},{"ClientRestoreMobile",9},
    {"ClientUseOffMap",10},{"ClientUseOffMap::Target",10},{"ClientWallActivate",10},{"ClientWallDeactivate",10},
};
std::unordered_map<uint32_t,int> cursorKinds;// CRC -> kind
std::unordered_map<uint32_t,const char*> cursorLabels;
uint32_t cursorLast=0;
void resetCursorState(){cursorKinds.clear();cursorLabels.clear();cursorLast=0;}
uint32_t engineCrc(const char* text){
    const auto* lower=static_cast<const uint8_t*>(kernelPtr(cursorCaseTable,256));
    const auto* table=static_cast<const uint32_t*>(kernelPtr(cursorCrcTable,1024));
    uint32_t crc=~0u;
    for(const char* c=text;*c;++c)crc=(crc<<8)^table[lower[uint8_t(*c)]^(crc>>24)];
    return ~crc;
}
// Crc of the cursor object pointer, or 0 when it is not registered.
uint32_t cursorCrcOf(uint32_t cursor){
    // The tree is small (a few dozen cursors); walk it iteratively.
    std::vector<uint32_t> stack{read32(cursorTree)};
    for(size_t guard=0;!stack.empty()&&guard<4096;++guard){
        uint32_t node=stack.back();stack.pop_back();
        if(!node||!activeImage->memory().readable(node,16))continue;
        if(read32(node+4)==cursor)return read32(node);
        stack.push_back(read32(node+8));stack.push_back(read32(node+12));
    }
    return 0;
}
void updateCursorState(){
    if(!runControl)return;
    runControl->displaySize.store((uint32_t(displayWidth)<<16)|uint32_t(displayHeight&0xffff));
    if(cursorKinds.empty())for(const auto& item:cursorNames){uint32_t crc=engineCrc(item.name);cursorKinds[crc]=item.kind;cursorLabels[crc]=item.name;}
    uint32_t cursor=read32(cursorCurrent);
    if(cursor==cursorLast)return;
    cursorLast=cursor;
    uint32_t crc=cursor?cursorCrcOf(cursor):0;
    auto found=cursorKinds.find(crc);int kind=found==cursorKinds.end()?(cursor?11:0):found->second;
    runControl->cursorKind.store(kind);
    if(cursorDebug){auto label=cursorLabels.find(crc);__android_log_print(ANDROID_LOG_INFO,"AMRTS","Cursor %08x crc %08x %s -> kind %d",cursor,crc,label==cursorLabels.end()?"?":label->second,kind);}
}
