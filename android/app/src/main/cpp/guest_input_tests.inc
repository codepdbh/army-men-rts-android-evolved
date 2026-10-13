void testGuestInput(){
    MachineState state;auto previousControl=runControl;runControl=std::make_shared<GuestRunControl>();
    uint32_t scratch=allocate(512),mouse=createInputObject(2),keyboard=createInputObject(3);
    auto require=[](bool value,const char* message){if(!value)throw GuestStop(message);};
    auto call=[&](uint32_t object,uint32_t method,std::initializer_list<uint32_t> parameters){
        uint32_t stack=kStackBase+kStackSize-256;g_esp=stack;write32(stack,RECOMP_RETADDR);write32(stack+4,object);
        uint32_t i=2;for(uint32_t value:parameters)write32(stack+i++*4,value);
        inputToken=read32(read32(object)+method*4);inputMethod();
        require(g_esp==stack+i*4,"DirectInput stdcall stack imbalance");return g_eax;
    };
    uint32_t format=scratch,objects=scratch+24,out=scratch+128,requested=scratch+384;
    uint32_t header[]={24,16,1,16,4,objects};std::memcpy(kernelPtr(format,24,true),header,24);
    uint32_t entries[]={0,0,0xffff03,0,0,4,0xffff03,0,0,8,0xffff03,0,0,12,0xffff0c,0};std::memcpy(kernelPtr(objects,64,true),entries,64);
    require(call(mouse,11,{format})==0&&call(mouse,7,{})==0,"Mouse format/acquire failed");
    inputObjects.at(mouse).bufferSize=16;
    header[3]=256;header[4]=1;std::memcpy(kernelPtr(format,24,true),header,24);
    uint32_t keyEntry[]={0,28,(28<<8)|4,0};std::memcpy(kernelPtr(objects,16,true),keyEntry,16);
    require(call(keyboard,11,{format})==0&&call(keyboard,7,{})==0,"Keyboard format/acquire failed");
    runControl->pointer(.25f,.25f,0,true);runControl->key(28,true);runControl->pointer(.25f,.25f,0,false);pumpAndroidInput();
    require(call(keyboard,9,{256,out})==0&&MEM8(out+28)==128,"Android Enter was not delivered to DirectInput");
    require(call(mouse,9,{16,out})==0&&MEM8(out+12)==0&&int32_t(read32(out))>0,"Mouse motion/release state mismatch");
    write32(requested,16);require(call(mouse,10,{16,out,requested,1})==0&&read32(requested)==4,"Mouse buffered events missing");
    require(read32(out+32)==12&&read32(out+36)==128&&read32(out+48)==12&&read32(out+52)==0,"Buffered tap transition order changed");
    require(inputObjects.at(mouse).events.size()==4,"Peek consumed DirectInput events");
    write32(requested,16);call(mouse,10,{16,out,requested,0});require(inputObjects.at(mouse).events.empty(),"DirectInput events did not drain");
    runControl->wheel(1);pumpAndroidInput();require(call(mouse,9,{16,out})==0&&read32(out+8)==120,"Pinch wheel did not reach DirectInput Z axis");
    call(mouse,8,{});require(call(mouse,9,{16,out})==0x8007000cu,"Unacquired mouse accepted state reads");
    call(mouse,2,{});call(keyboard,2,{});heaps->free(GuestHeaps::process,scratch);
    resetInput();runControl=previousControl;cursorX=cursorY=0;state.restore();
    __android_log_print(ANDROID_LOG_INFO,"AMRTS","Android pointer/keyboard and DirectInput buffer tests passed");
}
