enum class GraphicsKind : uint32_t { Draw1=1,Draw7=2,Direct3D7=3 };
struct GraphicsOwner {
    uint32_t references=1,identity=0,window=0,cooperative=0,width=1280,height=720,bits=32,refresh=60;
    std::map<GraphicsKind,uint32_t> interfaces;
};
struct GraphicsInterface { GraphicsKind kind;std::shared_ptr<GraphicsOwner> owner; };
std::map<uint32_t,GraphicsInterface> graphicsObjects;
std::map<GraphicsKind,uint32_t> graphicsVtables;
uint32_t graphicsToken=0;
void graphicsMethod();
uint32_t interfaceMethods(GraphicsKind kind){return kind==GraphicsKind::Draw1?23:kind==GraphicsKind::Draw7?30:8;}
uint32_t graphicsInterface(const std::shared_ptr<GraphicsOwner>& owner,GraphicsKind kind){
    auto found=owner->interfaces.find(kind);if(found!=owner->interfaces.end())return found->second;
    uint32_t table=0;auto existing=graphicsVtables.find(kind);
    if(existing!=graphicsVtables.end())table=existing->second;
    else{
        uint32_t count=interfaceMethods(kind);table=allocate(count*4);
        for(uint32_t i=0;i<count;++i)write32(table+i*4,0x75000000+uint32_t(kind)*0x10000+i*16);
        graphicsVtables.emplace(kind,table);
    }
    uint32_t object=allocate(8);write32(object,table);owner->interfaces.emplace(kind,object);
    graphicsObjects.emplace(object,GraphicsInterface{kind,owner});if(!owner->identity)owner->identity=object;return object;
}
bool guidEquals(uint32_t address,const DxGuid& guid){return address&&std::memcmp(kernelPtr(address,16),&guid,16)==0;}
recomp_func_t graphicsLookup(uint32_t va){
    if(va<0x75010000||va>=0x75040000||(va&15))return nullptr;
    uint32_t kind=(va-0x75000000)/0x10000,index=(va&0xffff)/16;
    if(index>=interfaceMethods(GraphicsKind(kind)))return nullptr;
    graphicsToken=va;return graphicsMethod;
}
uint32_t graphicsVideoMemory(){return uint32_t(std::min<uint64_t>(graphics->gpu().deviceMemory,256u<<20));}
uint32_t graphicsDepthBits(){
    uint32_t bits=0;
    if(graphics->supportsOptimalFormat(VK_FORMAT_D16_UNORM,VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))bits|=0x400;
    if(graphics->supportsOptimalFormat(VK_FORMAT_D32_SFLOAT,VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))bits|=0x100;
    return bits;
}
DxSurfaceDesc displayMode(const GraphicsOwner& owner,GraphicsKind kind,uint32_t width,uint32_t height,uint32_t bits){
    DxSurfaceDesc mode{};mode.size=kind==GraphicsKind::Draw1?108:124;mode.flags=0x4100e;
    mode.width=width;mode.height=height;mode.pitch=width*(bits/8);mode.mips=owner.refresh;
    mode.format.size=32;mode.format.flags=0x40;mode.format.bits=bits;
    mode.format.r=bits==16?0xf800:0xff0000;mode.format.g=bits==16?0x07e0:0x00ff00;mode.format.b=bits==16?0x001f:0x0000ff;
    return mode;
}
DxDeviceDesc7 deviceDescription(){
    DxDeviceDesc7 caps{};caps.caps=0x00088e10;
    DxPrimitiveCaps primitive{};primitive.size=56;primitive.misc=0x70;
    primitive.raster=0x000041f1;primitive.zCompare=0xff;primitive.sourceBlend=primitive.destinationBlend=0x7ff;
    primitive.alphaCompare=0xff;primitive.shade=0x0002420a;primitive.texture=0x00000045;
    primitive.textureFilter=0x0303033f;primitive.textureBlend=0xff;primitive.textureAddress=0x0f;
    caps.lines=caps.triangles=primitive;caps.renderBits=0x500;caps.zBits=graphicsDepthBits();
    caps.minWidth=caps.minHeight=1;caps.maxWidth=caps.maxHeight=std::min(graphics->gpu().maxTextureSize,2048u);
    caps.maxRepeat=caps.maxAspect=2048;caps.maxAnisotropy=1;
    caps.guardLeft=caps.guardTop=-1;caps.guardRight=caps.guardBottom=1;
    caps.fvf=2;caps.textureOps=0xffff;caps.blendStages=caps.simultaneousTextures=2;
    caps.activeLights=8;caps.maxVertexW=1e10f;caps.guid=iidHalDevice;return caps;
}
void graphicsMethod(){
    uint32_t token=graphicsToken,index=(token&0xffff)/16;
    GraphicsKind kind=GraphicsKind((token-0x75000000)/0x10000);
    auto found=graphicsObjects.find(arg(0));if(found==graphicsObjects.end()||found->second.kind!=kind)
        throw GuestStop("Invalid DirectDraw/Direct3D object at 0x"+hex(arg(0)));
    auto owner=found->second.owner;
    __android_log_print(ANDROID_LOG_DEBUG,"AMRTS","DX7 interface=%u method=%u caller=%08x",uint32_t(kind),index,g_cur_func);
    if(index==0){
        uint32_t result=0;
        if(guidEquals(arg(1),iidUnknown))result=owner->identity;
        else if(guidEquals(arg(1),iidDraw1))result=graphicsInterface(owner,GraphicsKind::Draw1);
        else if(guidEquals(arg(1),iidDraw7))result=graphicsInterface(owner,GraphicsKind::Draw7);
        else if(guidEquals(arg(1),iidDirect3D7))result=graphicsInterface(owner,GraphicsKind::Direct3D7);
        if(result)++owner->references;write32(arg(2),result);ret(result?0:0x80004002u,3);return;
    }
    if(index==1){ret(++owner->references,1);return;}
    if(index==2){
        if(!owner->references)throw GuestStop("Graphics COM reference underflow");
        uint32_t references=--owner->references;
        if(!references){for(const auto& item:owner->interfaces){graphicsObjects.erase(item.second);heaps->free(GuestHeaps::process,item.second);}owner->interfaces.clear();}
        ret(references,1);return;
    }
    if(kind==GraphicsKind::Direct3D7){
        if(index==3){
            uint32_t description=allocate(236),name=allocate(32);DxDeviceDesc7 caps=deviceDescription();
            std::memcpy(kernelPtr(description,236,true),&caps,236);const char label[]="Android Vulkan HAL";
            std::memcpy(kernelPtr(name,sizeof label,true),label,sizeof label);
            callGuest(arg(1),{graphicsDescription,name,description,arg(2)});
            heaps->free(GuestHeaps::process,description);heaps->free(GuestHeaps::process,name);ret(0,3);return;
        }
        if(index==6){
            uint32_t callback=arg(2),context=arg(3),format=allocate(32);
            for(uint32_t bits:{16u,32u}){
                if(!(graphicsDepthBits()&(bits==16?0x400:0x100)))continue;
                DxPixelFormat pixel{};pixel.size=32;pixel.flags=0x400;pixel.bits=bits;pixel.g=bits==16?0xffff:UINT32_MAX;
                std::memcpy(kernelPtr(format,32,true),&pixel,32);if(!callGuest(callback,{format,context}))break;
            }
            heaps->free(GuestHeaps::process,format);ret(0,4);return;
        }
        throw GuestStop("Direct3D7 method pending: "+std::to_string(index)+" from 0x"+hex(g_cur_func));
    }
    if(index==11){
        for(uint32_t output:{arg(1),arg(2)})if(output){
            uint32_t size=read32(output);if(size<100||size>380){ret(0x80070057u,3);return;}
            std::vector<uint32_t> caps((size+3)/4);caps[0]=size;caps[1]=0x84000241u;caps[3]=0x100;
            caps[14]=graphicsDepthBits();caps[15]=caps[16]=graphicsVideoMemory();
            std::memcpy(kernelPtr(output,size,true),caps.data(),size);
        }
        ret(0,3);return;
    }
    if(index==8){
        uint32_t callback=arg(4),context=arg(3),filter=arg(2),description=allocate(124);
        const uint32_t dimensions[][2]={{640,480},{800,600},{1024,768},{1280,720},{1280,960},{1280,1024}};
        bool finished=false;
        for(uint32_t bits:{16u,32u}){for(const auto& dimensionsItem:dimensions){
            DxSurfaceDesc mode=displayMode(*owner,kind,dimensionsItem[0],dimensionsItem[1],bits);
            if(filter){uint32_t flags=read32(filter+4);
                if((flags&4)&&read32(filter+12)!=mode.width)continue;
                if((flags&2)&&read32(filter+8)!=mode.height)continue;
                if((flags&0x1000)&&read32(filter+84)!=bits)continue;}
            std::memcpy(kernelPtr(description,mode.size,true),&mode,mode.size);
            if(!callGuest(callback,{description,context})){finished=true;break;}
        }if(finished)break;}
        heaps->free(GuestHeaps::process,description);ret(0,5);return;
    }
    if(index==12){
        DxSurfaceDesc mode=displayMode(*owner,kind,owner->width,owner->height,owner->bits);
        if(read32(arg(1))!=mode.size){ret(0x80070057u,2);return;}
        std::memcpy(kernelPtr(arg(1),mode.size,true),&mode,mode.size);ret(0,2);return;
    }
    if(index==13){write32(arg(1),0);ret(0,3);return;}
    if(index==15){write32(arg(1),owner->refresh);ret(0,2);return;}
    if(index==16||index==17){
        uint64_t nanos=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        uint32_t line=uint32_t((nanos%(1000000000/owner->refresh))*owner->height/(1000000000/owner->refresh));
        write32(arg(1),index==16?line:line+1>=owner->height);ret(0,2);return;
    }
    if(index==20){
        if(arg(1)&&!windows.count(arg(1))){ret(0x80070057u,3);return;}
        owner->window=arg(1);owner->cooperative=arg(2);ret(0,3);return;
    }
    if(index==21){
        uint32_t width=arg(1),height=arg(2),bits=arg(3),count=kind==GraphicsKind::Draw1?4:6;
        if(!width||!height||width>graphics->gpu().maxTextureSize||height>graphics->gpu().maxTextureSize||(bits!=16&&bits!=32)){
            ret(0x80070057u,count);return;
        }
        owner->width=width;owner->height=height;owner->bits=bits;
        if(kind!=GraphicsKind::Draw1&&arg(4))owner->refresh=arg(4);
        displayWidth=width;displayHeight=height;
        if(owner->window&&windows.count(owner->window)){windows.at(owner->window).width=width;windows.at(owner->window).height=height;}
        ret(0,count);return;
    }
    if(index==23){if(arg(2))write32(arg(2),graphicsVideoMemory());if(arg(3))write32(arg(3),graphicsVideoMemory());ret(0,4);return;}
    if(index==27){
        uint32_t out=arg(1);std::memset(kernelPtr(out,1068,true),0,1068);
        const char driver[]="vulkan";std::memcpy(kernelPtr(out,sizeof driver,true),driver,sizeof driver);
        std::string name=graphics->gpu().name;name.resize(std::min<size_t>(name.size(),511));
        std::memcpy(kernelPtr(out+512,name.size()+1,true),name.c_str(),name.size()+1);
        write32(out+1028,0x00010001);write32(out+1032,graphics->gpu().vendorId);write32(out+1036,graphics->gpu().deviceId);
        DxGuid id=iidDraw7;id.a^=graphics->gpu().vendorId;std::memcpy(kernelPtr(out+1048,16,true),&id,16);ret(0,3);return;
    }
    if(index==3||index==10||index==19||index==25||index==26){ret(0,1);return;}
    static const char* drawNames[]={"QueryInterface","AddRef","Release","Compact","CreateClipper","CreatePalette","CreateSurface","DuplicateSurface","EnumDisplayModes","EnumSurfaces","FlipToGDISurface","GetCaps","GetDisplayMode","GetFourCCCodes","GetGDISurface","GetMonitorFrequency","GetScanLine","GetVerticalBlankStatus","Initialize","RestoreDisplayMode","SetCooperativeLevel","SetDisplayMode","WaitForVerticalBlank","GetAvailableVidMem","GetSurfaceFromDC","RestoreAllSurfaces","TestCooperativeLevel","GetDeviceIdentifier","StartModeTest","EvaluateMode"};
    throw GuestStop(std::string("DirectDraw method pending: ")+drawNames[index]+" from 0x"+hex(g_cur_func));
}
