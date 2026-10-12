#include <jni.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <sys/stat.h>
#include <string>

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
    uint32_t version = VK_API_VERSION_1_0;
    auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    VkResult result = enumerate ? enumerate(&version) : VK_SUCCESS;
    char gpu[128];
    std::snprintf(gpu, sizeof(gpu), "Vulkan: %u.%u (mínimo 1.1)\n",
                  VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version));
    report += result == VK_SUCCESS ? gpu : "Vulkan: consulta fallida\n";
    report += "Recompilación ARM64 y puente DirectDraw/Direct3D 7 en desarrollo.";
    return env->NewStringUTF(report.c_str());
}
