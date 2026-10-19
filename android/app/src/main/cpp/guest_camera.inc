// Direct camera control for touch panning.
//
// The engine's camera simulation is a virtual method (0x0056E7A0, thiscall,
// reached only through its vtable, so it can be intercepted via the manual
// lookup). Its object (layout measured on device):
//   +0x068 right, +0x078 up, +0x088 front   rendered orientation (unit vectors)
//   +0x098 position the view is rendered from (eases toward +0x1C0)
//   +0x0E0 current height above terrain, +0x0F4 requested height
//   +0x1C0 target position (x, y, z); camera.movex/movez add to it
// 0x00542AB0(ecx=&x, edx=&z) clamps a point to the map and returns the terrain
// height there in ST(0). The projection lives in the video camera as a D3D
// matrix (m00 = 1/tan(hfov/2), m11 = 1/tan(vfov/2), m23 = 1).
//
// A drag anchors the terrain point under the finger and, every simulated
// frame, translates both target and rendered positions so that point stays
// under the finger: exact 1:1 tracking without the engine's ease-in lag.
// Releasing with speed continues the virtual finger with exponential decay.
extern "C" void sub_0056E7A0(void);
extern "C" void sub_00542AB0(void);
extern "C" void sub_004E07E0(void);// VarFloat::operator F32 (thiscall, result in ST(0))
constexpr uint32_t cameraMinHeightVar=0x0076F4D0,cameraMaxHeightVar=0x0076F4E0;
constexpr uint32_t cameraSimulate=0x0056E7A0;
const int cameraDebug=[]{char value[PROP_VALUE_MAX]{};__system_property_get("debug.amrts.camera",value);return std::atoi(value);}();
struct CameraVec {float x=0,y=0,z=0;};
struct CameraPan {
    uint32_t serial=0;bool anchored=false,flinging=false,flingTaken=false;
    CameraVec anchor;float fingerX=0,fingerY=0,velocityX=0,velocityY=0,startHeight=0;
    std::chrono::steady_clock::time_point last;
};
uint32_t cameraObject=0,cameraScratch=0,projectionAddress=0;
float cameraTanX=0.7438f,cameraTanY=0.5579f;
CameraPan cameraPan;
std::chrono::steady_clock::time_point cameraDumpTime,projectionScanTime;
void resetCamera(){cameraObject=cameraScratch=projectionAddress=0;cameraPan=CameraPan{};cameraTanX=0.7438f;cameraTanY=0.5579f;projectionScanTime={};}
float cameraFloat(uint32_t camera,uint32_t offset){float value;std::memcpy(&value,kernelPtr(camera+offset,4),4);return value;}
void setCameraFloat(uint32_t camera,uint32_t offset,float value){std::memcpy(kernelPtr(camera+offset,4,true),&value,4);}
CameraVec cameraVector(uint32_t camera,uint32_t offset){return {cameraFloat(camera,offset),cameraFloat(camera,offset+4),cameraFloat(camera,offset+8)};}

bool projectionAt(uint32_t va,float& tanX,float& tanY){
    if(!va||!activeImage->memory().readable(va,64))return false;
    float m[16];std::memcpy(m,activeImage->address(va,64),64);
    if(m[11]!=1.0f||m[1]!=0||m[2]!=0||m[3]!=0||m[4]!=0||m[6]!=0||m[7]!=0||m[8]!=0||m[9]!=0||m[12]!=0||m[13]!=0||m[15]!=0)return false;
    if(!(m[0]>.2f&&m[0]<5&&m[5]>.2f&&m[5]<5&&m[10]>.5f&&m[10]<2&&m[14]<0))return false;
    tanX=1/m[0];tanY=1/m[5];return true;
}
// The perspective matrix is the only match with a negative m32 (identity
// placeholders share the zero pattern). It is re-located if it moves.
void locateProjection(){
    float tanX,tanY;
    if(projectionAt(projectionAddress,tanX,tanY)){cameraTanX=tanX;cameraTanY=tanY;return;}
    auto now=std::chrono::steady_clock::now();if(now-projectionScanTime<std::chrono::seconds(5))return;projectionScanTime=now;
    projectionAddress=0;
    activeImage->memory().forEachCommitted([&](uint64_t start,uint64_t end){
        for(uint64_t chunk=start;chunk<end&&!projectionAddress;chunk+=0x10000){
            uint64_t limit=std::min<uint64_t>(chunk+0x10000,end);
            if(!activeImage->memory().readable(uint32_t(chunk),limit-chunk))continue;
            for(uint64_t va=chunk;va+64<=limit;va+=4)if(projectionAt(uint32_t(va),tanX,tanY)){projectionAddress=uint32_t(va);break;}
        }
    });
    if(projectionAddress){cameraTanX=tanX;cameraTanY=tanY;}
    __android_log_print(ANDROID_LOG_INFO,"AMRTS","Camera projection %08x: tan %.4f x %.4f",projectionAddress,cameraTanX,cameraTanY);
}

// Clamp (x, z) to the playable map and return the terrain height there, using
// the engine's own routine on a scratch copy.
float cameraGroundAt(float& x,float& z){
    if(!cameraScratch)cameraScratch=allocate(8);
    std::memcpy(kernelPtr(cameraScratch,4,true),&x,4);std::memcpy(kernelPtr(cameraScratch+4,4,true),&z,4);
    MachineState state;
    g_ecx=cameraScratch;g_edx=cameraScratch+4;g_esp-=4;write32(g_esp,RECOMP_RETADDR);
    sub_00542AB0();
    float height=float(g_st[0]);state.restore();
    std::memcpy(&x,kernelPtr(cameraScratch,4),4);std::memcpy(&z,kernelPtr(cameraScratch+4,4),4);
    return std::isfinite(height)?height:0;
}

float cameraVarFloat(uint32_t var,float fallback){
    MachineState state;g_ecx=var;g_esp-=4;write32(g_esp,RECOMP_RETADDR);
    sub_004E07E0();float value=float(g_st[0]);state.restore();
    return std::isfinite(value)?value:fallback;
}

// Intersect the view ray through normalised screen point (sx, sy) with the
// horizontal plane at `planeY`, from the rendered camera pose.
bool cameraRay(uint32_t camera,float sx,float sy,float planeY,CameraVec& hit){
    CameraVec right=cameraVector(camera,0x68),up=cameraVector(camera,0x78),front=cameraVector(camera,0x88),eye=cameraVector(camera,0x98);
    float u=sx*2-1,v=1-sy*2;
    CameraVec dir{front.x+u*cameraTanX*right.x+v*cameraTanY*up.x,front.y+u*cameraTanX*right.y+v*cameraTanY*up.y,front.z+u*cameraTanX*right.z+v*cameraTanY*up.z};
    if(dir.y>-0.02f)return false;// At or above the horizon.
    float t=(planeY-eye.y)/dir.y;if(!(t>0&&t<4000))return false;
    hit={eye.x+dir.x*t,planeY,eye.z+dir.z*t};return true;
}

void applyCameraPan(uint32_t camera){
    if(!runControl)return;
    GuestRunControl::CameraDrag drag;
    {std::lock_guard<std::mutex> lock(runControl->cameraMutex);drag=runControl->cameraDrag;}
    auto& pan=cameraPan;auto now=std::chrono::steady_clock::now();
    if(drag.serial!=pan.serial){
        pan=CameraPan{};pan.serial=drag.serial;pan.last=now;
        if(drag.began){
            locateProjection();
            // Anchor on the real terrain: start from the ground below the eye,
            // then refine with the height at the hit point.
            CameraVec eye=cameraVector(camera,0x98);float x=eye.x,z=eye.z,plane=cameraGroundAt(x,z);
            // Anchor where the finger first touched, even if it has moved since.
            for(int i=0;i<3&&cameraRay(camera,drag.startX,drag.startY,plane,pan.anchor);++i){
                float hx=pan.anchor.x,hz=pan.anchor.z;plane=cameraGroundAt(hx,hz);pan.anchored=true;
            }
            if(pan.anchored)pan.anchored=cameraRay(camera,drag.startX,drag.startY,plane,pan.anchor);
            pan.startHeight=cameraFloat(camera,0xf4);
        }
    }
    if(!pan.anchored)return;
    float dt=std::min(0.1f,std::chrono::duration<float>(now-pan.last).count());pan.last=now;
    if(drag.active){
        pan.fingerX=drag.x;pan.fingerY=drag.y;
        if(drag.zoom!=1){
            // Spreading the fingers lowers the camera, within the engine's limits.
            // Writing current and requested height skips the wheel easing; the
            // anchor loop below keeps the point between the fingers in place.
            float low=cameraVarFloat(cameraMinHeightVar,20),high=cameraVarFloat(cameraMaxHeightVar,70);
            float height=std::clamp(pan.startHeight/drag.zoom,std::min(low,high),std::max(low,high));
            setCameraFloat(camera,0xf4,height);setCameraFloat(camera,0xe0,height);
        }
    }
    else if(!pan.flingTaken){
        pan.flingTaken=true;pan.fingerX=drag.x;pan.fingerY=drag.y;
        if(std::hypot(drag.vx,drag.vy)>0.25f){pan.flinging=true;pan.velocityX=drag.vx;pan.velocityY=drag.vy;}
    }else if(pan.flinging){
        pan.fingerX+=pan.velocityX*dt;pan.fingerY+=pan.velocityY*dt;
        float decay=std::exp(-dt/0.325f);pan.velocityX*=decay;pan.velocityY*=decay;
        if(std::hypot(pan.velocityX,pan.velocityY)<0.04f)pan.flinging=false;
    }else{pan.anchored=false;return;}
    CameraVec under;
    if(!cameraRay(camera,pan.fingerX,pan.fingerY,pan.anchor.y,under)){pan.flinging=false;return;}
    float dx=pan.anchor.x-under.x,dz=pan.anchor.z-under.z;
    if(std::fabs(dx)<1e-4f&&std::fabs(dz)<1e-4f)return;
    // Clamp with the engine's map bounds; move the rendered pose by the same
    // amount so the view follows the finger without the ease-in delay.
    float tx=cameraFloat(camera,0x1c0),tz=cameraFloat(camera,0x1c8),cx=tx+dx,cz=tz+dz;
    cameraGroundAt(cx,cz);
    float ax=cx-tx,az=cz-tz;
    setCameraFloat(camera,0x1c0,cx);setCameraFloat(camera,0x1c8,cz);
    setCameraFloat(camera,0x98,cameraFloat(camera,0x98)+ax);setCameraFloat(camera,0xa0,cameraFloat(camera,0xa0)+az);
    if(pan.flinging&&(std::fabs(ax-dx)>1e-3f||std::fabs(az-dz)>1e-3f))pan.flinging=false;// Hit the map edge.
}

void dumpObject(const char* label,uint32_t object,uint32_t size){
    std::string line;char item[48];
    if(!activeImage->memory().readable(object,size))return;
    for(uint32_t offset=0;offset<size;offset+=4){
        float value=cameraFloat(object,offset);uint32_t raw=read32(object+offset);
        if(std::isfinite(value)&&std::fabs(value)>1e-6f&&std::fabs(value)<1e7f)std::snprintf(item,sizeof item," %03x=%g",offset,value);
        else std::snprintf(item,sizeof item," %03x=#%08x",offset,raw);
        line+=item;
        if(line.size()>900){__android_log_print(ANDROID_LOG_INFO,"AMRTS","%s %08x:%s",label,object,line.c_str());line.clear();}
    }
    if(!line.empty())__android_log_print(ANDROID_LOG_INFO,"AMRTS","%s %08x:%s",label,object,line.c_str());
}
void cameraSimulateHook(){
    uint32_t camera=g_ecx;
    if(camera!=cameraObject){__android_log_print(ANDROID_LOG_INFO,"AMRTS","Camera object %08x vtable %08x",camera,read32(camera));cameraObject=camera;cameraPan=CameraPan{};}
    if(cameraDebug){auto now=std::chrono::steady_clock::now();if(now-cameraDumpTime>std::chrono::seconds(2)){cameraDumpTime=now;dumpObject("Camera",camera,0x240);}}
    applyCameraPan(camera);
    sub_0056E7A0();
}
recomp_func_t manualLookup(uint32_t va){return va==cameraSimulate?cameraSimulateHook:nullptr;}
