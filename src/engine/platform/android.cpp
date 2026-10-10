#include "engine/platform/android.h"

#ifdef __ANDROID__

#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>

#include <adrenotools/driver.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <jni.h>
#include <signal.h>
#include <unistd.h>
#include <unwind.h>

#include <cstdio>
#include <cstring>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

#include <android/log.h>

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

std::filesystem::path LogDir() {
    static const std::filesystem::path dir = [] {
        std::error_code ec;
        const std::filesystem::path shared = "/storage/emulated/0/Download/PT-logs";
        std::filesystem::create_directories(shared, ec);
        const std::filesystem::path probe = shared / ".write-test";
        if (FILE* f = std::fopen(probe.c_str(), "w")) {
            std::fclose(f);
            std::filesystem::remove(probe, ec);
            return shared;
        }
        const std::filesystem::path own = DataDir() / "data";
        std::filesystem::create_directories(own, ec);
        return own;
    }();
    return dir;
}

namespace {

int g_crash_fd = -1;
struct sigaction g_previous[32];

void WriteText(const char* text) {
    if (g_crash_fd >= 0) (void)!write(g_crash_fd, text, std::strlen(text));
}

struct Frames {
    void* pcs[64];
    int count = 0;
};

_Unwind_Reason_Code CollectFrame(_Unwind_Context* context, void* arg) {
    auto* frames = static_cast<Frames*>(arg);
    const uintptr_t pc = _Unwind_GetIP(context);
    if (pc && frames->count < 64) frames->pcs[frames->count++] = reinterpret_cast<void*>(pc);
    return frames->count < 64 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void OnCrash(int signal, siginfo_t* info, void* context) {
    char line[512];
    std::snprintf(line, sizeof(line), "P.T. crashed: signal %d (%s), fault address %p\n", signal, strsignal(signal),
                  info ? info->si_addr : nullptr);
    WriteText(line);
    __android_log_write(ANDROID_LOG_FATAL, "pt", line);
    Frames frames;
    _Unwind_Backtrace(CollectFrame, &frames);
    for (int i = 0; i < frames.count; ++i) {
        Dl_info dl{};
        if (dladdr(frames.pcs[i], &dl) && dl.dli_fname) {
            const uintptr_t offset = reinterpret_cast<uintptr_t>(frames.pcs[i]) - reinterpret_cast<uintptr_t>(dl.dli_fbase);
            std::snprintf(line, sizeof(line), "  #%02d %s +0x%zx %s\n", i, dl.dli_fname, static_cast<size_t>(offset),
                          dl.dli_sname ? dl.dli_sname : "");
        } else {
            std::snprintf(line, sizeof(line), "  #%02d %p\n", i, frames.pcs[i]);
        }
        WriteText(line);
    }
    if (g_crash_fd >= 0) fsync(g_crash_fd);
    // hand the signal on (Android's debuggerd writes its tombstone, the process ends)
    sigaction(signal, &g_previous[signal], nullptr);
    (void)context;
    raise(signal);
}

}  // namespace

void InstallCrashLog(const std::filesystem::path& path) {
    g_crash_fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    struct sigaction action{};
    action.sa_sigaction = OnCrash;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE}) {
        sigaction(signal, &action, &g_previous[signal]);
    }
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
