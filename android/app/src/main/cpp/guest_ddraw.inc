std::unique_ptr<VulkanRenderer> graphics;
uint32_t graphicsDescription=0,graphicsDriverName=0;
void resetGraphicsObjects();
void resetGraphics() { if(graphics)graphics->flushDraws();resetGraphicsObjects();graphics.reset();graphicsDescription=graphicsDriverName=0; }
void initializeGraphics() {
    if(graphics)return;
    auto renderer=std::make_unique<VulkanRenderer>();renderer->initialize();renderer->testClearReadback();renderer->testTriangleReadback();
    renderer->setWindow(presentationWindow);
    std::string description=renderer->gpu().name+" / Vulkan 1.1";
    graphicsDescription=allocate(uint32_t(description.size()+1));
    std::memcpy(kernelPtr(graphicsDescription,description.size()+1,true),description.c_str(),description.size()+1);
    const char driver[]="Vulkan";graphicsDriverName=allocate(sizeof driver);
    std::memcpy(kernelPtr(graphicsDriverName,sizeof driver,true),driver,sizeof driver);
    graphics=std::move(renderer);
}
#include "guest_ddraw_objects.inc"
void resetGraphicsObjects(){graphicsObjects.clear();graphicsVtables.clear();graphicsToken=0;}
bool ddrawDispatch(const std::string& name) {
    if(name=="DirectDrawCreate"||name=="DirectDrawCreateEx"){
        initializeGraphics();uint32_t count=name=="DirectDrawCreate"?3:4;
        write32(arg(1),0);if(arg(count-1)){ret(0x80040110u,count);return true;}
        GraphicsKind kind=name=="DirectDrawCreate"?GraphicsKind::Draw1:GraphicsKind::Draw7;
        if(name=="DirectDrawCreateEx"&&!guidEquals(arg(2),iidDraw7)){ret(0x80004002u,count);return true;}
        auto owner=std::make_shared<GraphicsOwner>();uint32_t object=graphicsInterface(owner,kind);
        write32(arg(1),object);ret(0,count);return true;
    }
    if(name=="DirectDrawEnumerateExA"||name=="DirectDrawEnumerateA"){
        initializeGraphics();uint32_t callback=arg(0),context=arg(1);
        __android_log_print(ANDROID_LOG_INFO,"AMRTS","DirectDraw enum callback=%08x context=%08x",callback,context);
        if(!callback){ret(0x80070057u,name=="DirectDrawEnumerateExA"?3:2);return true;}
        if(name=="DirectDrawEnumerateExA")callGuest(callback,{0,graphicsDescription,graphicsDriverName,context,0});
        else callGuest(callback,{0,graphicsDescription,graphicsDriverName,context});
        ret(0,name=="DirectDrawEnumerateExA"?3:2);return true;
    }
    return false;
}
