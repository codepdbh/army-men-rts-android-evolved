enum class GraphicsKind : uint32_t { Draw1=1,Draw7=2,Direct3D7=3,Surface7=4,Device7=5,VertexBuffer7=6 };
struct GraphicsOwner {
    uint32_t references=1,identity=0,window=0,cooperative=0,width=1280,height=720,bits=32,refresh=60;
    std::map<GraphicsKind,uint32_t> interfaces;
    std::shared_ptr<GraphicsOwner> parent;
    std::shared_ptr<VulkanSurface> image;
    DxSurfaceDesc description{};
    uint32_t pixels=0,target=0;
    bool locked=false,readOnly=false,gpuWritten=false,renderTarget=false;
    std::vector<uint32_t> attached;
    std::vector<uint8_t> viewport=std::vector<uint8_t>(24),material=std::vector<uint8_t>(68);
    std::map<uint32_t,std::vector<uint8_t>> transforms,lights;
    std::map<uint32_t,uint32_t> renderStates,stageStates,textures,enabledLights;
    bool scene=false;
    uint32_t vertexDesc[4]{},vertexBytes=0;
};
struct GraphicsInterface { GraphicsKind kind;std::shared_ptr<GraphicsOwner> owner; };
std::map<uint32_t,GraphicsInterface> graphicsObjects;
std::map<GraphicsKind,uint32_t> graphicsVtables;
uint32_t graphicsToken=0;
void graphicsMethod();
uint32_t interfaceMethods(GraphicsKind kind){return kind==GraphicsKind::Draw1?23:kind==GraphicsKind::Draw7?30:kind==GraphicsKind::Direct3D7?8:kind==GraphicsKind::VertexBuffer7?9:49;}
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
uint32_t vertexStride(uint32_t fvf){
    uint32_t position=fvf&0xe,stride=position==2?12:position==4?16:0;
    if(!stride||fvf&0x10)throw GuestStop("Vertex FVF pending: 0x"+hex(fvf));
    if(fvf&0x20)stride+=12;if(fvf&0x40)stride+=4;if(fvf&0x80)stride+=4;
    uint32_t count=(fvf>>8)&15;if(count>8)throw GuestStop("Invalid texture coordinate count");
    for(uint32_t i=0;i<count;++i){uint32_t format=(fvf>>(16+i*2))&3;stride+=(format==0?2:format==1?3:format==2?4:1)*4;}
    return stride;
}
uint32_t graphicsRelease(uint32_t object){
    auto found=graphicsObjects.find(object);if(found==graphicsObjects.end())throw GuestStop("Invalid graphics release");
    auto owner=found->second.owner;if(!owner->references)throw GuestStop("Graphics COM reference underflow");
    uint32_t count=--owner->references;if(count)return count;
    if(graphics)graphics->flushDraws();
    for(const auto& texture:owner->textures)if(texture.second)graphicsRelease(texture.second);owner->textures.clear();
    for(uint32_t child:owner->attached)graphicsRelease(child);owner->attached.clear();
    for(const auto& item:owner->interfaces){graphicsObjects.erase(item.second);heaps->free(GuestHeaps::process,item.second);}owner->interfaces.clear();
    if(owner->pixels)heaps->free(GuestHeaps::process,owner->pixels);owner->pixels=0;owner->image.reset();
    if(owner->parent){uint32_t parent=owner->parent->identity;owner->parent.reset();graphicsRelease(parent);}
    return 0;
}
void uploadGuestSurface(GraphicsOwner& owner){
    auto& desc=owner.description;if(desc.caps[0]&0x20000)throw GuestStop("CPU depth buffer upload pending");
    const uint8_t* source=static_cast<const uint8_t*>(kernelPtr(owner.pixels,uint64_t(desc.pitch)*desc.height));
    std::vector<uint8_t> rgba(uint64_t(desc.width)*desc.height*4);
    auto component=[](uint32_t value,uint32_t mask,uint8_t fallback){
        if(!mask)return fallback;uint32_t shift=__builtin_ctz(mask),maximum=mask>>shift;
        return uint8_t((((value&mask)>>shift)*255u+maximum/2)/maximum);
    };
    for(uint32_t y=0;y<desc.height;++y)for(uint32_t x=0;x<desc.width;++x){
        uint32_t pixel=0;std::memcpy(&pixel,source+uint64_t(y)*desc.pitch+x*(desc.format.bits/8),desc.format.bits/8);
        uint64_t out=(uint64_t(y)*desc.width+x)*4;rgba[out]=component(pixel,desc.format.r,0);
        rgba[out+1]=component(pixel,desc.format.g,0);rgba[out+2]=component(pixel,desc.format.b,0);rgba[out+3]=component(pixel,desc.format.a,255);
    }
    if(textureDump&&desc.format.a==0xf000){
        // debug.amrts.dumptex 1: save alpha textures as TGA for inspection.
        static int dumped=0;
        if(dumped<400){char path[256];std::snprintf(path,sizeof path,"%s/texdump/%03d_%ux%u.tga",hostGameRoot.c_str(),dumped++,desc.width,desc.height);
            std::filesystem::create_directories(hostGameRoot+"/texdump");
            if(FILE* file=std::fopen(path,"wb")){uint8_t header[18]{};header[2]=2;header[12]=desc.width&255;header[13]=desc.width>>8;header[14]=desc.height&255;header[15]=desc.height>>8;header[16]=32;header[17]=0x28;
                std::fwrite(header,1,18,file);for(size_t i=0;i<rgba.size();i+=4){uint8_t bgra[4]={rgba[i+2],rgba[i+1],rgba[i],rgba[i+3]};std::fwrite(bgra,1,4,file);}std::fclose(file);}}
    }
    graphics->uploadSurface(*owner.image,rgba.data());owner.gpuWritten=false;
}
void downloadGuestSurface(GraphicsOwner& owner){
    if(!owner.gpuWritten)return;
    auto& desc=owner.description;auto rgba=graphics->readSurface(*owner.image);
    auto* target=static_cast<uint8_t*>(kernelPtr(owner.pixels,uint64_t(desc.pitch)*desc.height,true));
    auto component=[](uint8_t value,uint32_t mask){if(!mask)return 0u;uint32_t shift=__builtin_ctz(mask),maximum=mask>>shift;return ((uint32_t(value)*maximum+127)/255)<<shift;};
    for(uint32_t y=0;y<desc.height;++y)for(uint32_t x=0;x<desc.width;++x){uint64_t offset=(uint64_t(y)*desc.width+x)*4;
        uint32_t pixel=component(rgba[offset],desc.format.r)|component(rgba[offset+1],desc.format.g)|component(rgba[offset+2],desc.format.b)|component(rgba[offset+3],desc.format.a);
        std::memcpy(target+uint64_t(y)*desc.pitch+x*(desc.format.bits/8),&pixel,desc.format.bits/8);}
    owner.gpuWritten=false;
}
bool guidEquals(uint32_t address,const DxGuid& guid){return address&&std::memcmp(kernelPtr(address,16),&guid,16)==0;}
recomp_func_t graphicsLookup(uint32_t va){
    if(va<0x75010000||va>=0x75070000||(va&15))return nullptr;
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
    DxDeviceDesc7 caps{};caps.caps=0x00088e11;
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
void graphicsDraw(const std::shared_ptr<GraphicsOwner>& device,uint32_t primitive,uint32_t fvf,uint32_t data,uint32_t count,uint32_t indexData,uint32_t indexCount){
    if(count>65536||indexCount>1048576)throw GuestStop("Draw count outside supported range");
    uint32_t stride=vertexStride(fvf);auto* source=static_cast<const uint8_t*>(kernelPtr(data,uint64_t(stride)*count));
    auto target=graphicsObjects.at(device->target).owner;
    uint32_t viewport[6];std::memcpy(viewport,device->viewport.data(),24);float minZ,maxZ;std::memcpy(&minZ,viewport+4,4);std::memcpy(&maxZ,viewport+5,4);
    if(!viewport[2]||!viewport[3])throw GuestStop("Draw viewport missing");
    std::vector<VulkanVertex> vertices(count);
    // debug.amrts.tint 1: colour each draw by its stage-0 texture format
    // (565 magenta, 4444 cyan, 1555 yellow) to identify what draws what.
    uint32_t tintFormat=0;
    if(drawTint==2){
        auto render=[&](uint32_t index,uint32_t fallback){auto found=device->renderStates.find(index);return found==device->renderStates.end()?fallback:found->second;};
        auto stage=device->stageStates.find(1);uint32_t op=stage==device->stageStates.end()?4:stage->second;
        auto texture=device->textures.find(0);bool rtt=texture!=device->textures.end()&&texture->second&&graphicsObjects.at(texture->second).owner->renderTarget;
        tintFormat=rtt?0x0000ff:op==5?0xff0000:render(27,0)&&render(20,1)==2?0x00ff00:0;
    }else if(drawTint==3){
        auto render=[&](uint32_t index,uint32_t fallback){auto found=device->renderStates.find(index);return found==device->renderStates.end()?fallback:found->second;};
        auto texture=device->textures.find(0);bool untextured=texture==device->textures.end()||!texture->second;
        tintFormat=untextured&&render(27,0)?0xffff00:0;
    }else if(drawTint){auto texture=device->textures.find(0);
        if(texture!=device->textures.end()&&texture->second){const auto& f=graphicsObjects.at(texture->second).owner->description.format;
            tintFormat=f.bits==16&&!f.a?0xff00ff:f.a==0xf000?0x00ffff:f.a==0x8000?0xffff00:0;}}
    auto unpack=[](uint32_t color,float* rgba){rgba[0]=((color>>16)&255)/255.0f;rgba[1]=((color>>8)&255)/255.0f;rgba[2]=(color&255)/255.0f;rgba[3]=(color>>24)/255.0f;};
    auto transform=[&](float* value,uint32_t state){
        auto found=device->transforms.find(state);if(found==device->transforms.end())return;
        float matrix[16],output[4]{};std::memcpy(matrix,found->second.data(),64);
        for(uint32_t j=0;j<4;++j)for(uint32_t k=0;k<4;++k)output[j]+=value[k]*matrix[k*4+j];std::memcpy(value,output,16);
    };
    for(uint32_t i=0;i<count;++i){
        const uint8_t* input=source+uint64_t(i)*stride;auto& vertex=vertices[i];std::memcpy(vertex.position,input,12);vertex.position[3]=1;
        uint32_t offset=(fvf&0xe)==4?16:12;
        if((fvf&0xe)==4){float rhw;std::memcpy(&rhw,input+12,4);float w=rhw?1.0f/rhw:1;
            vertex.position[0]=((vertex.position[0]-viewport[0]+0.5f)*2/viewport[2]-1)*w;
            vertex.position[1]=((vertex.position[1]-viewport[1]+0.5f)*2/viewport[3]-1)*w;
            vertex.position[2]=(vertex.position[2]-minZ)/(maxZ-minZ)*w;vertex.position[3]=w;
        }else{transform(vertex.position,256);transform(vertex.position,2);transform(vertex.position,3);vertex.position[1]=-vertex.position[1];}
        if(fvf&0x20)offset+=12;
        uint32_t color=UINT32_MAX;if(fvf&0x40){std::memcpy(&color,input+offset,4);offset+=4;}
        if(tintFormat){color=(color&0xff000000u)|tintFormat;}
        unpack(color,vertex.color);
        if(fvf&0x80)offset+=4;
        uint32_t textures=(fvf>>8)&15;
        for(uint32_t t=0;t<textures;++t){uint32_t format=(fvf>>(16+t*2))&3,n=format==0?2:format==1?3:format==2?4:1;
            if(t<2){float* out=t?vertex.uv1:vertex.uv0;std::memcpy(out,input+offset,std::min(n,2u)*4);}offset+=n*4;}
    }
    std::vector<uint16_t> indices(indexCount);
    if(indexData)std::memcpy(indices.data(),kernelPtr(indexData,uint64_t(indexCount)*2),indexCount*2);
    else {indices.resize(count);for(uint32_t i=0;i<count;++i)indices[i]=uint16_t(i);}
    for(uint16_t index:indices)if(index>=count)throw GuestStop("Draw index outside vertex buffer");
    VulkanDrawState state;state.primitive=primitive;
    auto render=[&](uint32_t index,uint32_t fallback){auto found=device->renderStates.find(index);return found==device->renderStates.end()?fallback:found->second;};
    state.cull=render(22,3);state.blend=render(27,0);state.sourceBlend=render(19,2);state.destinationBlend=render(20,1);
    state.zTest=render(7,1);state.zWrite=render(14,1);state.zCompare=render(23,4);
    state.alpha[0]=render(15,0);state.alpha[1]=render(25,8);state.alpha[2]=render(24,0);unpack(render(60,UINT32_MAX),state.factor);
    state.viewport[0]=viewport[0];state.viewport[1]=viewport[1];state.viewport[2]=viewport[2];state.viewport[3]=viewport[3];state.viewport[4]=minZ;state.viewport[5]=maxZ;
    VulkanSurface* textures[2]{};
    for(uint32_t stage=0;stage<2;++stage){
        auto get=[&](uint32_t key,uint32_t fallback){auto found=device->stageStates.find((stage<<16)|key);return found==device->stageStates.end()?fallback:found->second;};
        state.stages[stage*4]=get(1,stage?1:4);state.stages[stage*4+1]=get(2,2);state.stages[stage*4+2]=get(3,stage?1:0);
        state.stages[stage*4+3]=get(4,stage?1:2)|(get(5,2)<<8)|(get(6,stage?1:0)<<16);
        // D3DTSS_ADDRESS (12) sets both; ADDRESSU (13) / ADDRESSV (14) override.
        state.address[stage*2]=get(13,1);state.address[stage*2+1]=get(14,1);
        auto texture=device->textures.find(stage);if(texture!=device->textures.end()&&texture->second)textures[stage]=graphicsObjects.at(texture->second).owner->image.get();
    }
    VulkanSurface* depth=nullptr;for(uint32_t child:target->attached)if(graphicsObjects.at(child).owner->description.caps[0]&0x20000){depth=graphicsObjects.at(child).owner->image.get();break;}
    if(traceCalls)__android_log_print(ANDROID_LOG_DEBUG,"AMRTS","Draw primitive=%u fvf=%08x vertices=%u indices=%u",primitive,fvf,count,uint32_t(indices.size()));
    if(stateTrace){
        // Log every distinct render-state combination once (debug.amrts.states 1).
        auto format=[&](uint32_t stage){auto texture=device->textures.find(stage);if(texture==device->textures.end()||!texture->second)return std::string("-");
            const auto& f=graphicsObjects.at(texture->second).owner->description.format;char text[64];std::snprintf(text,sizeof text,"%u:%x/%x/%x/%x",f.bits,f.r,f.g,f.b,f.a);return std::string(text);};
        char key[320];std::snprintf(key,sizeof key,"blend=%u %u>%u atest=%u/%u/%u s0=%u(%u,%u)a%x s1=%u(%u,%u)a%x tex0=%s tex1=%s fvf=%x",
            state.blend,state.sourceBlend,state.destinationBlend,state.alpha[0],state.alpha[1],state.alpha[2],
            state.stages[0],state.stages[1],state.stages[2],state.stages[3],state.stages[4],state.stages[5],state.stages[6],state.stages[7],format(0).c_str(),format(1).c_str(),fvf);
        static std::set<std::string> seen;if(seen.insert(key).second)__android_log_print(ANDROID_LOG_INFO,"AMRTS","DrawState %s",key);
    }
    graphics->draw(*target->image,depth,textures[0],textures[1],vertices,indices,state);target->gpuWritten=true;
}
void graphicsMethod(){
    uint32_t token=graphicsToken,index=(token&0xffff)/16;
    GraphicsKind kind=GraphicsKind((token-0x75000000)/0x10000);
    auto found=graphicsObjects.find(arg(0));if(found==graphicsObjects.end()||found->second.kind!=kind)
        throw GuestStop("Invalid DirectDraw/Direct3D object at 0x"+hex(arg(0)));
    auto owner=found->second.owner;
    if(traceCalls&&index!=20&&index!=21&&index!=35&&index!=36&&index!=37)__android_log_print(ANDROID_LOG_DEBUG,"AMRTS","DX7 interface=%u method=%u caller=%08x",uint32_t(kind),index,g_cur_func);
    if(index==0){
        uint32_t result=0;
        if(guidEquals(arg(1),iidUnknown))result=owner->identity;
        else if((kind==GraphicsKind::Draw1||kind==GraphicsKind::Draw7||kind==GraphicsKind::Direct3D7)&&guidEquals(arg(1),iidDraw1))result=graphicsInterface(owner,GraphicsKind::Draw1);
        else if((kind==GraphicsKind::Draw1||kind==GraphicsKind::Draw7||kind==GraphicsKind::Direct3D7)&&guidEquals(arg(1),iidDraw7))result=graphicsInterface(owner,GraphicsKind::Draw7);
        else if((kind==GraphicsKind::Draw1||kind==GraphicsKind::Draw7||kind==GraphicsKind::Direct3D7)&&guidEquals(arg(1),iidDirect3D7))result=graphicsInterface(owner,GraphicsKind::Direct3D7);
        if(kind==GraphicsKind::Surface7&&guidEquals(arg(1),iidSurface7))result=owner->identity;
        if(kind==GraphicsKind::Device7&&guidEquals(arg(1),iidDevice7))result=owner->identity;
        if(kind==GraphicsKind::VertexBuffer7&&guidEquals(arg(1),iidVertexBuffer7))result=owner->identity;
        if(result)++owner->references;write32(arg(2),result);ret(result?0:0x80004002u,3);return;
    }
    if(index==1){ret(++owner->references,1);return;}
    if(index==2){ret(graphicsRelease(arg(0)),1);return;}
    if(kind==GraphicsKind::VertexBuffer7){
        if(index==3){if(owner->locked){ret(0x80070057u,4);return;}owner->locked=true;write32(arg(2),owner->pixels);if(arg(3))write32(arg(3),owner->vertexBytes);ret(0,4);return;}
        if(index==4){bool locked=owner->locked;owner->locked=false;ret(locked?0:0x80070057u,1);return;}
        if(index==6){std::memcpy(kernelPtr(arg(1),16,true),owner->vertexDesc,16);ret(0,2);return;}
        if(index==7){ret(0,3);return;}
        throw GuestStop("VertexBuffer7 method pending: "+std::to_string(index));
    }
    if(kind==GraphicsKind::Surface7){
        if(index==8){
            if(arg(1)){ret(0x80070057u,3);return;}
            auto found=std::find(owner->attached.begin(),owner->attached.end(),arg(2));
            if(found==owner->attached.end()){ret(0x887600ffu,3);return;}
            uint32_t child=*found;owner->attached.erase(found);graphicsRelease(child);ret(0,3);return;
        }
        if(index==9){
            uint32_t context=arg(1),callback=arg(2),description=allocate(124);
            auto children=owner->attached;
            for(uint32_t child:children){auto attached=graphicsObjects.at(child).owner;
                std::memcpy(kernelPtr(description,124,true),&attached->description,124);++attached->references;
                if(!callGuest(callback,{child,description,context}))break;}
            heaps->free(GuestHeaps::process,description);ret(0,3);return;
        }
        if(index==11){
            uint32_t target=arg(1);if(!target){for(uint32_t child:owner->attached)if(graphicsObjects.at(child).owner->description.caps[0]&4){target=child;break;}}
            auto back=graphicsObjects.find(target);if(back==graphicsObjects.end()||back->second.kind!=GraphicsKind::Surface7){ret(0x80070057u,3);return;}
            auto& buffer=*back->second.owner;
            graphics->present(*buffer.image);updateCursorState();if(runControl)++runControl->frames;std::swap(owner->image,buffer.image);std::swap(owner->pixels,buffer.pixels);std::swap(owner->gpuWritten,buffer.gpuWritten);ret(0,3);return;
        }
        if(index==5){
            uint32_t flags=arg(4);if(flags&~0x01000400u)throw GuestStop("Surface Blt flags pending: 0x"+hex(flags));
            auto& desc=owner->description;int32_t rectangle[4]={0,0,int32_t(desc.width),int32_t(desc.height)};
            if(arg(1))std::memcpy(rectangle,kernelPtr(arg(1),16),16);
            if(rectangle[0]<0||rectangle[1]<0||rectangle[2]>int32_t(desc.width)||rectangle[3]>int32_t(desc.height)||rectangle[2]<=rectangle[0]||rectangle[3]<=rectangle[1]){ret(0x80070057u,6);return;}
            if(!(flags&0x400))throw GuestStop("Surface source Blt pending");
            if(!arg(5)||read32(arg(5))!=100){ret(0x80070057u,6);return;}
            downloadGuestSurface(*owner);uint32_t color=read32(arg(5)+80),bytes=desc.format.bits/8;
            auto* pixels=static_cast<uint8_t*>(kernelPtr(owner->pixels,uint64_t(desc.pitch)*desc.height,true));
            for(int32_t y=rectangle[1];y<rectangle[3];++y)for(int32_t x=rectangle[0];x<rectangle[2];++x)
                std::memcpy(pixels+uint64_t(y)*desc.pitch+x*bytes,&color,bytes);
            uploadGuestSurface(*owner);ret(0,6);return;
        }
        if(index==3){
            auto child=graphicsObjects.find(arg(1));if(child==graphicsObjects.end()||child->second.kind!=GraphicsKind::Surface7){ret(0x80070057u,2);return;}
            if(child->second.owner.get()==owner.get()){ret(0x80070057u,2);return;}
            if(std::find(owner->attached.begin(),owner->attached.end(),arg(1))==owner->attached.end()){owner->attached.push_back(arg(1));++child->second.owner->references;}
            ret(0,2);return;
        }
        if(index==12){
            uint32_t caps=read32(arg(1));for(uint32_t child:owner->attached){auto attached=graphicsObjects.at(child).owner;
                if((attached->description.caps[0]&caps)==caps){++attached->references;write32(arg(2),child);ret(0,3);return;}}
            write32(arg(2),0);ret(0x887600ffu,3);return;
        }
        if(index==22||index==25){
            uint32_t output=arg(index==25?2:1);uint32_t size=read32(output);
            if(size!=124){ret(0x80070057u,index==25?5:2);return;}
            DxSurfaceDesc description=owner->description;
            if(index==25){
                if(arg(1))throw GuestStop("Surface rectangle lock pending");if(owner->locked){ret(0x80070057u,5);return;}
                // This exact Flip path locks/unlocks solely to flush D3D;
                // it never accesses lpSurface. Vulkan already synchronizes it.
                bool flushOnly=g_cur_func==0x4200f0&&arg(3)==1;
                if(!flushOnly&&!(arg(3)&0x2000))downloadGuestSurface(*owner);
                owner->locked=true;owner->readOnly=flushOnly||(arg(3)&0x10)!=0;
                description.surface=owner->pixels;description.flags|=0x800;
            }
            std::memcpy(kernelPtr(output,124,true),&description,124);ret(0,index==25?5:2);return;
        }
        if(index==21){std::memcpy(kernelPtr(arg(1),32,true),&owner->description.format,32);ret(0,2);return;}
        if(index==14){std::memcpy(kernelPtr(arg(1),16,true),owner->description.caps,16);ret(0,2);return;}
        if(index==24||index==27){ret(0,1);return;}
        if(index==32){if(!owner->locked){ret(0x88760248u,2);return;}if(!owner->readOnly)uploadGuestSurface(*owner);owner->locked=false;ret(0,2);return;}
        throw GuestStop("DirectDrawSurface7 method pending: "+std::to_string(index)+" from 0x"+hex(g_cur_func));
    }
    if(kind==GraphicsKind::Device7){
        if(index==31||index==32){
            auto found=graphicsObjects.find(arg(2));if(found==graphicsObjects.end()||found->second.kind!=GraphicsKind::VertexBuffer7){ret(0x80070057u,index==32?8:6);return;}
            auto buffer=found->second.owner;uint32_t start=arg(3),count=arg(4),fvf=buffer->vertexDesc[2];
            if(buffer->locked||uint64_t(start)+count>buffer->vertexDesc[3]){ret(0x80070057u,index==32?8:6);return;}
            graphicsDraw(owner,arg(1),fvf,buffer->pixels+start*vertexStride(fvf),count,index==32?arg(5):0,index==32?arg(6):count);
            ret(0,index==32?8:6);return;
        }
        if(index==7){uint32_t object=graphicsInterface(owner->parent->parent,GraphicsKind::Direct3D7);++owner->parent->parent->references;write32(arg(1),object);ret(0,2);return;}
        if(index==8){
            auto target=graphicsObjects.find(arg(1));if(target==graphicsObjects.end()||target->second.kind!=GraphicsKind::Surface7){ret(0x80070057u,3);return;}
            auto old=owner->parent;owner->parent=target->second.owner;target->second.owner->renderTarget=true;++owner->parent->references;owner->target=arg(1);graphicsRelease(old->identity);ret(0,3);return;
        }
        if(index==14){
            uint32_t transform=arg(1);float right[16],left[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},result[16]{};
            std::memcpy(right,kernelPtr(arg(2),64),64);auto found=owner->transforms.find(transform);if(found!=owner->transforms.end())std::memcpy(left,found->second.data(),64);
            for(uint32_t row=0;row<4;++row)for(uint32_t col=0;col<4;++col)for(uint32_t k=0;k<4;++k)result[row*4+col]+=right[row*4+k]*left[k*4+col];
            auto& record=owner->transforms[transform];record.resize(64);std::memcpy(record.data(),result,64);ret(0,3);return;
        }
        if(index==25||index==26){
            graphicsDraw(owner,arg(1),arg(2),arg(3),arg(4),index==26?arg(5):0,index==26?arg(6):arg(4));ret(0,index==26?8:6);return;
        }
        if(index==10){
            uint32_t flags=arg(3),color=arg(4),rawDepth=arg(5);float depth;std::memcpy(&depth,&rawDepth,4);
            auto target=graphicsObjects.at(owner->target).owner;
            if(arg(1)){
                uint32_t count=arg(1);if(count>256){ret(0x80070057u,7);return;}
                auto* rectangles=static_cast<const int32_t*>(kernelPtr(arg(2),uint64_t(count)*16));std::vector<VkRect2D> regions;
                for(uint32_t i=0;i<count;++i){int32_t left=std::clamp(rectangles[i*4],0,int32_t(target->description.width)),top=std::clamp(rectangles[i*4+1],0,int32_t(target->description.height));
                    int32_t right=std::clamp(rectangles[i*4+2],0,int32_t(target->description.width)),bottom=std::clamp(rectangles[i*4+3],0,int32_t(target->description.height));
                    if(right>left&&bottom>top)regions.push_back({{left,top},{uint32_t(right-left),uint32_t(bottom-top)}});}
                VulkanSurface* z=nullptr;for(uint32_t child:target->attached){auto surface=graphicsObjects.at(child).owner;if(surface->description.caps[0]&0x20000){z=surface->image.get();break;}}
                if((flags&2)&&!z){ret(0x80070057u,7);return;}if(flags&4)throw GuestStop("Stencil clear pending");
                graphics->clearRegions(*target->image,z,flags,color,depth,regions);if(flags&1)target->gpuWritten=true;ret(0,7);return;
            }
            if(flags&4)throw GuestStop("Stencil clear pending");
            if(flags&1){graphics->clearSurface(*target->image,color,depth);target->gpuWritten=true;}
            if(flags&2){bool found=false;for(uint32_t child:target->attached){auto surface=graphicsObjects.at(child).owner;
                if(surface->description.caps[0]&0x20000){graphics->clearSurface(*surface->image,0,depth);found=true;break;}}
                if(!found){ret(0x80070057u,7);return;}}
            ret(0,7);return;
        }
        if(index==5||index==6){
            if(owner->scene==(index==5)){ret(0x8876086cu,1);return;}owner->scene=index==5;ret(0,1);return;
        }
        if(index==13||index==15||index==16||index==17){
            auto& record=(index==13||index==15)?owner->viewport:owner->material;
            if(index==13||index==16)std::memcpy(record.data(),kernelPtr(arg(1),record.size()),record.size());
            else std::memcpy(kernelPtr(arg(1),record.size(),true),record.data(),record.size());
            ret(0,2);return;
        }
        if(index==11||index==12||index==18||index==19){
            auto& records=(index==11||index==12)?owner->transforms:owner->lights;
            uint32_t size=(index==11||index==12)?64:104,key=arg(1);
            auto& record=records[key];if(record.empty()){record.resize(size);if(size==64){float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};std::memcpy(record.data(),identity,64);}}
            if(index==11||index==18)std::memcpy(record.data(),kernelPtr(arg(2),size),size);
            else std::memcpy(kernelPtr(arg(2),size,true),record.data(),size);
            ret(0,3);return;
        }
        if(index==20||index==21||index==36||index==37||index==44||index==45){
            bool stage=index==36||index==37;
            auto& states=stage?owner->stageStates:(index==44||index==45)?owner->enabledLights:owner->renderStates;
            uint32_t key=stage?((arg(1)<<16)|arg(2)):arg(1),valueIndex=stage?3:2;
            if(index==20||index==37||index==44){
                states[key]=arg(valueIndex);
                // D3DTSS_ADDRESS sets both D3DTSS_ADDRESSU and D3DTSS_ADDRESSV.
                if(stage&&arg(2)==12){states[(arg(1)<<16)|13]=arg(valueIndex);states[(arg(1)<<16)|14]=arg(valueIndex);}
            }
            else write32(arg(valueIndex),states[key]);ret(0,stage?4:3);return;
        }
        if(index==35||index==34){
            uint32_t stage=arg(1);if(stage>=8){ret(0x80070057u,3);return;}
            if(index==35){uint32_t texture=arg(2);
                if(texture&&(!graphicsObjects.count(texture)||graphicsObjects.at(texture).kind!=GraphicsKind::Surface7)){
                    if(stateTrace){static int rejected=0;if(rejected++<20)__android_log_print(ANDROID_LOG_WARN,"AMRTS","SetTexture rejected %08x kind %d from %08x",texture,graphicsObjects.count(texture)?int(graphicsObjects.at(texture).kind):-1,g_cur_func);}
                    ret(0x80070057u,3);return;}
                uint32_t previous=owner->textures[stage];if(texture)++graphicsObjects.at(texture).owner->references;
                owner->textures[stage]=texture;if(previous)graphicsRelease(previous);
            }else{uint32_t texture=owner->textures[stage];if(texture)++graphicsObjects.at(texture).owner->references;write32(arg(2),texture);}
            ret(0,3);return;
        }
        if(index==38){write32(arg(1),1);ret(0,2);return;}
        if(index==9){++owner->parent->references;write32(arg(1),owner->target);ret(0,2);return;}

        if(index==3){auto caps=deviceDescription();std::memcpy(kernelPtr(arg(1),236,true),&caps,236);ret(0,2);return;}
        if(index==4){
            uint32_t callback=arg(1),context=arg(2),output=allocate(32);
            const DxPixelFormat formats[]={
                {32,0x40,0,16,0xf800,0x7e0,0x1f,0},
                {32,0x41,0,16,0x7c00,0x3e0,0x1f,0x8000},
                {32,0x41,0,16,0xf00,0xf0,0xf,0xf000},
                {32,0x40,0,32,0xff0000,0xff00,0xff,0},
                {32,0x41,0,32,0xff0000,0xff00,0xff,0xff000000}};
            for(const auto& format:formats){std::memcpy(kernelPtr(output,32,true),&format,32);if(!callGuest(callback,{output,context}))break;}
            heaps->free(GuestHeaps::process,output);ret(0,3);return;
        }
        throw GuestStop("Direct3DDevice7 method pending: "+std::to_string(index)+" from 0x"+hex(g_cur_func));
    }
    if(kind==GraphicsKind::Direct3D7){
        if(index==7){ret(0,1);return;}
        if(index==5){
            uint32_t descriptor=arg(1);if(read32(descriptor)!=16){ret(0x80070057u,4);return;}
            uint32_t stride=vertexStride(read32(descriptor+8)),count=read32(descriptor+12);
            uint64_t bytes=uint64_t(stride)*count;if(!bytes||bytes>128u*1024*1024){ret(0x80070057u,4);return;}
            auto buffer=std::make_shared<GraphicsOwner>();buffer->parent=owner;++owner->references;
            std::memcpy(buffer->vertexDesc,kernelPtr(descriptor,16),16);buffer->vertexBytes=uint32_t(bytes);buffer->pixels=allocate(uint32_t(bytes));
            write32(arg(2),graphicsInterface(buffer,GraphicsKind::VertexBuffer7));ret(0,4);return;
        }
        if(index==4){
            auto surface=graphicsObjects.find(arg(2));
            if(surface==graphicsObjects.end()||surface->second.kind!=GraphicsKind::Surface7){ret(0x80070057u,4);return;}
            auto device=std::make_shared<GraphicsOwner>();device->parent=surface->second.owner;++device->parent->references;device->target=arg(2);
            write32(arg(3),graphicsInterface(device,GraphicsKind::Device7));ret(0,4);return;
        }
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
    if(index==6){
        if(arg(3)){ret(0x80040110u,4);return;}
        DxSurfaceDesc description{};uint32_t size=read32(arg(1));
        if(size!=124||kind!=GraphicsKind::Draw7){ret(0x80070057u,4);return;}
        std::memcpy(&description,kernelPtr(arg(1),124),124);
        if(!(description.flags&4))description.width=owner->width;
        if(!(description.flags&2))description.height=owner->height;
        if(!(description.flags&0x1000))description.format=displayMode(*owner,kind,description.width,description.height,owner->bits).format;
        uint32_t bits=description.format.bits;
        if(!description.width||!description.height||description.width>graphics->gpu().maxTextureSize||description.height>graphics->gpu().maxTextureSize||(bits!=16&&bits!=32)){ret(0x80070057u,4);return;}
        if(description.backbuffers>3){ret(0x80070057u,4);return;}
        uint64_t pitch=(uint64_t(description.width)*(bits/8)+3)&~uint64_t(3),bytes=pitch*description.height;
        if(bytes>128u*1024*1024){ret(0x8007000eu,4);return;}
        auto surface=std::make_shared<GraphicsOwner>();surface->parent=owner;++owner->references;surface->description=description;
        surface->description.pitch=uint32_t(pitch);surface->description.flags|=0x100f;
        surface->image=graphics->createSurface(description.width,description.height,(description.caps[0]&0x20000)!=0,bits);
        surface->pixels=allocate(uint32_t(bytes));
        for(uint32_t i=0;i<description.backbuffers;++i){
            auto back=std::make_shared<GraphicsOwner>();back->parent=owner;++owner->references;back->description=surface->description;
            back->description.backbuffers=0;back->description.caps[0]=(description.caps[0]&~0x220u)|4;
            back->image=graphics->createSurface(description.width,description.height,false,bits);back->pixels=allocate(uint32_t(bytes));
            surface->attached.push_back(graphicsInterface(back,GraphicsKind::Surface7));
        }
        if(description.caps[0]&0x400000){
            uint32_t width=description.width,height=description.height,levels=description.mips;
            if(!(description.flags&0x20000)){levels=1;for(uint32_t edge=std::max(width,height);edge>1;edge>>=1)++levels;}
            if(levels>16)throw GuestStop("Invalid mipmap count");
            auto previous=surface;
            for(uint32_t level=1;level<levels;++level){
                width=std::max(1u,width/2);height=std::max(1u,height/2);
                auto mip=std::make_shared<GraphicsOwner>();mip->parent=owner;++owner->references;mip->description=surface->description;
                mip->description.width=width;mip->description.height=height;mip->description.pitch=(width*(bits/8)+3)&~3u;
                mip->description.mips=levels-level;mip->description.caps[1]|=0x1000;
                mip->image=graphics->createSurface(width,height,false,bits);mip->pixels=allocate(mip->description.pitch*height);
                previous->attached.push_back(graphicsInterface(mip,GraphicsKind::Surface7));previous=mip;
            }
        }
        write32(arg(2),graphicsInterface(surface,GraphicsKind::Surface7));
        __android_log_print(ANDROID_LOG_INFO,"AMRTS","Surface %ux%u %u bits caps=%08x",description.width,description.height,bits,description.caps[0]);
        ret(0,4);return;
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
        const uint32_t dimensions[][2]={{640,480},{800,600},{1024,768},{1280,720},{1280,960},{1280,1024},
            {1600,900},{1600,1200},{1920,1080},{2340,1080},{2560,1440}};
        static_assert(sizeof(dimensions)/sizeof(dimensions[0])*2<=22,"Original engine video-mode table capacity");
        bool finished=false;
        for(uint32_t bits:{16u,32u}){for(const auto& dimensionsItem:dimensions){
            if(dimensionsItem[0]>graphics->gpu().maxTextureSize||dimensionsItem[1]>graphics->gpu().maxTextureSize)continue;
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
        uint64_t nanos=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(guestClock()).count());
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
        __android_log_print(ANDROID_LOG_INFO,"AMRTS","Display mode %ux%u %u bits",width,height,bits);
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
