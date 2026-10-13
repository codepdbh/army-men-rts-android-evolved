#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <sys/stat.h>
#include <string>
#include "pe_image.h"
#include "vulkan_renderer.h"
#include "guest_run_control.h"
#include <map>
#include <memory>
#ifdef AMRTS_LINK_GUEST
#include "guest_machine.h"
extern "C" {
#include "recomp_types.h"
}
#endif

extern "C" JNIEXPORT jstring JNICALL
Java_com_codepdbh_armymenrts_LauncherActivity_nativeBootProbe(JNIEnv* env, jclass, jstring dataPath) {
#ifdef AMRTS_LINK_GUEST
    const char* chars = env->GetStringUTFChars(dataPath, nullptr);
    std::string root(chars);
    env->ReleaseStringUTFChars(dataPath, chars);
    try {
        PeImage image;
        PeImageInfo info = image.load(root + "/AMRTS.EXE");
        std::string result = probeGuestBoot(image, info);
        __android_log_print(ANDROID_LOG_INFO, "AMRTS", "Boot probe: %s", result.c_str());
        return env->NewStringUTF(result.c_str());
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, "AMRTS", "Boot probe: %s", error.what());
        return env->NewStringUTF(error.what());
    }
#else
    return env->NewStringUTF("Genera los C y compila con -PamrtsLinkGuest");
#endif
}

namespace {
bool regular(const std::string& path) {
    struct stat info{};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_codepdbh_armymenrts_LauncherActivity_nativeProbe(JNIEnv* env, jclass, jstring dataPath) {
    const char* chars = env->GetStringUTFChars(dataPath, nullptr);
    std::string root(chars);
    env->ReleaseStringUTFChars(dataPath, chars);
    std::string report;
    for (const char* path : {"AMRTS.EXE", "base.x", "binkw32.dll", "mss32.dll"}) {
        report += std::string(path) + (regular(root + "/" + path) ? ": OK\n" : ": falta\n");
    }
    if (regular(root + "/AMRTS.EXE")) {
        try {
            PeImage image;
            PeImageInfo info = image.load(root + "/AMRTS.EXE");
            char pe[160];
            std::snprintf(pe, sizeof(pe),
                          "PE32 x86: %u secciones, %u importaciones, entrada 0x%08x\n",
                          info.sections, info.imports, info.entry);
            report += pe;
            if (info.importTable.size() != info.imports)
                report += "Error: tabla de importaciones incompleta\n";
            else
                report += "Enlaces de importacion x86 preparados\n";
#ifdef AMRTS_LINK_GUEST
            if (recomp_lookup(info.entry)) {
                char lifted[100];
                std::snprintf(lifted, sizeof(lifted),
                              "Codigo ARM64 enlazado: %u funciones, entrada encontrada\n",
                              recomp_dispatch_count);
                report += lifted;
            } else {
                report += "Error: falta la entrada en el codigo traducido\n";
            }
#endif
        } catch (const std::exception& error) {
            report += std::string("PE32: ") + error.what() + "\n";
        }
    }
    try {
        VulkanRenderer renderer;renderer.initialize(false);
        const auto& info=renderer.gpu();char gpu[192];
        std::snprintf(gpu,sizeof gpu,"GPU: %s / Vulkan %u.%u (minimo 1.1)\n",info.name.c_str(),
                      VK_API_VERSION_MAJOR(info.apiVersion),VK_API_VERSION_MINOR(info.apiVersion));
        report+=gpu;
    }catch(const std::exception& error){report+=std::string("GPU: ")+error.what()+"\n";}
    report += "Recompilación ARM64 y puente DirectDraw/Direct3D 7 en desarrollo.";
    return env->NewStringUTF(report.c_str());
}

namespace {
std::mutex sessionMutex;
std::map<uint64_t,std::shared_ptr<GuestRunControl>> sessions;
uint64_t nextSession=1;
std::shared_ptr<GuestRunControl> session(uint64_t id){std::lock_guard<std::mutex> lock(sessionMutex);auto found=sessions.find(id);return found==sessions.end()?nullptr:found->second;}
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_codepdbh_armymenrts_GameProbeActivity_nativeCreateSession(JNIEnv*,jclass){
    std::lock_guard<std::mutex> lock(sessionMutex);uint64_t id=nextSession++;sessions.emplace(id,std::make_shared<GuestRunControl>());return jlong(id);
}
extern "C" JNIEXPORT void JNICALL
Java_com_codepdbh_armymenrts_GameProbeActivity_nativeCloseSession(JNIEnv*,jclass,jlong id){
    std::lock_guard<std::mutex> lock(sessionMutex);auto found=sessions.find(uint64_t(id));if(found!=sessions.end()){found->second->stopped.store(true);sessions.erase(found);}
}
extern "C" JNIEXPORT void JNICALL
Java_com_codepdbh_armymenrts_GameProbeActivity_nativePointer(JNIEnv*,jclass,jlong id,jfloat x,jfloat y,jint button,jboolean down){
    auto control=session(uint64_t(id));if(control)control->pointer(x,y,uint32_t(button),bool(down));
}
extern "C" JNIEXPORT void JNICALL
Java_com_codepdbh_armymenrts_GameProbeActivity_nativeKey(JNIEnv*,jclass,jlong id,jint scan,jboolean down){
    auto control=session(uint64_t(id));if(control)control->key(uint32_t(scan),bool(down));
}
extern "C" JNIEXPORT void JNICALL
Java_com_codepdbh_armymenrts_GameProbeActivity_nativeWheel(JNIEnv*,jclass,jlong id,jint steps){auto control=session(uint64_t(id));if(control)control->wheel(steps);}
extern "C" JNIEXPORT jint JNICALL
Java_com_codepdbh_armymenrts_GameProbeActivity_nativeFrames(JNIEnv*,jclass,jlong id){auto control=session(uint64_t(id));return control?jint(control->frames.load()):0;}
extern "C" JNIEXPORT jstring JNICALL
Java_com_codepdbh_armymenrts_GameProbeActivity_nativeVisualProbe(JNIEnv* env,jclass,jstring dataPath,jobject surface,jlong id){
#ifdef AMRTS_LINK_GUEST
    auto control=session(uint64_t(id));if(!control)return env->NewStringUTF("Sesion detenida");
    const char* chars=env->GetStringUTFChars(dataPath,nullptr);std::string root(chars);env->ReleaseStringUTFChars(dataPath,chars);
    ANativeWindow* window=ANativeWindow_fromSurface(env,surface);if(!window)return env->NewStringUTF("Android surface unavailable");
    std::string result;
    try{PeImage image;PeImageInfo info=image.load(root+"/AMRTS.EXE");result=probeGuestBoot(image,info,window,control);}
    catch(const std::exception& error){result=error.what();}
    ANativeWindow_release(window);__android_log_print(ANDROID_LOG_INFO,"AMRTS","Visual run: %s",result.c_str());return env->NewStringUTF(result.c_str());
#else
    return env->NewStringUTF("Compile with -PamrtsLinkGuest");
#endif
}
