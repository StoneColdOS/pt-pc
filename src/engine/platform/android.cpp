#include "engine/platform/android.h"

#ifdef __ANDROID__

#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>

#include <adrenotools/driver.h>
#include <dlfcn.h>
#include <jni.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

#include "engine/core/log.h"

namespace pt::android {
namespace {

std::string Trim(std::string text) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    return text;
}

std::string ReadAsset(const char* name, bool& ok) {
    size_t size = 0;
    void* data = SDL_LoadFile(name, &size);
    ok = data != nullptr;
    if (!data) return {};
    std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    return text;
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// getApplicationInfo().nativeLibraryDir: where the APK's libraries were unpacked (useLegacyPackaging), and so where
// libadrenotools' hook libraries and the bundled Turnip driver are
std::string NativeLibraryDir() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return {};
    std::string result;
    jclass activity_class = env->GetObjectClass(activity);
    jmethodID get_info = env->GetMethodID(activity_class, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;");
    jobject info = get_info ? env->CallObjectMethod(activity, get_info) : nullptr;
    if (info) {
        jclass info_class = env->GetObjectClass(info);
        jfieldID field = env->GetFieldID(info_class, "nativeLibraryDir", "Ljava/lang/String;");
        auto dir = field ? static_cast<jstring>(env->GetObjectField(info, field)) : nullptr;
        if (dir) {
            const char* chars = env->GetStringUTFChars(dir, nullptr);
            result = chars ? chars : "";
            env->ReleaseStringUTFChars(dir, chars);
            env->DeleteLocalRef(dir);
        }
        env->DeleteLocalRef(info_class);
        env->DeleteLocalRef(info);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(activity_class);
    env->DeleteLocalRef(activity);
    return result;
}

}  // namespace

std::filesystem::path DataDir() {
    static const std::filesystem::path dir = [] {
        const char* external = SDL_GetAndroidExternalStoragePath();
        if (external && *external) return std::filesystem::path(external);
        const char* internal = SDL_GetAndroidInternalStoragePath();
        return std::filesystem::path(internal ? internal : ".");
    }();
    return dir;
}

bool UnpackBundledAssets() {
    bool ok = false;
    const std::string manifest = ReadAsset("pt_assets.txt", ok);
    if (!ok) {
        LogError("android: the APK has no pt_assets.txt ({})", SDL_GetError());
        return false;
    }
    const std::filesystem::path stamp = DataDir() / ".pt_assets.txt";
    std::error_code ec;
    if (std::filesystem::exists(stamp, ec) && ReadTextFile(stamp) == manifest) {
        return true;
    }
    LogInfo("android: unpacking the bundled shaders and fonts into {}", DataDir().string());
    std::istringstream lines(manifest);
    std::string line;
    int count = 0;
    while (std::getline(lines, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        bool read = false;
        const std::string data = ReadAsset(line.c_str(), read);
        if (!read) {
            LogError("android: cannot read the asset {} ({})", line, SDL_GetError());
            return false;
        }
        const std::filesystem::path target = DataDir() / line;
        std::filesystem::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out) {
            LogError("android: cannot write {}", target.string());
            return false;
        }
        ++count;
    }
    std::ofstream(stamp, std::ios::binary | std::ios::trunc) << manifest;
    LogInfo("android: unpacked {} files", count);
    return true;
}

namespace {

PFN_vkGetInstanceProcAddr OpenVulkanDriver(std::string& name) {
    std::string choice = "turnip";
    {
        std::ifstream in(DataDir() / "driver.txt");
        std::string line;
        if (std::getline(in, line)) {
            line = Trim(line);
            std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!line.empty()) choice = line;
        }
    }
    if (choice == "system") {
        name = "system driver (driver.txt)";
        return nullptr;
    }
    const std::string lib_dir = NativeLibraryDir();
    if (lib_dir.empty()) {
        LogWarn("android: nativeLibraryDir unknown, using the system driver");
        name = "system driver (no library folder)";
        return nullptr;
    }
    const char* cache = SDL_GetAndroidCachePath();
    const std::string dir = lib_dir + "/";
    void* handle = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, cache, dir.c_str(), dir.c_str(),
                                              "libvulkan_turnip.so", nullptr, nullptr);
    if (!handle) {
        const char* error = dlerror();
        LogWarn("android: libadrenotools could not load Turnip ({}), using the system driver", error ? error : "no error text");
        name = "system driver (Turnip failed to load)";
        return nullptr;
    }
    auto proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(handle, "vkGetInstanceProcAddr"));
    if (!proc) {
        LogWarn("android: Turnip has no vkGetInstanceProcAddr, using the system driver");
        name = "system driver (Turnip incomplete)";
        return nullptr;
    }
    name = "Mesa Turnip (bundled)";
    return proc;
}

}  // namespace

PFN_vkGetInstanceProcAddr LoadVulkanDriver(std::string& name) {
    // opened once per process: a second libadrenotools load would map the driver again
    static std::string chosen;
    static const PFN_vkGetInstanceProcAddr proc = OpenVulkanDriver(chosen);
    name = chosen;
    return proc;
}

}  // namespace pt::android

#endif
