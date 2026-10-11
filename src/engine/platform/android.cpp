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
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <atomic>
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

bool HasAllFilesAccess() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env || SDL_GetAndroidSDKVersion() < 30) return true;
    bool result = false;
    if (jclass environment = env->FindClass("android/os/Environment")) {
        if (jmethodID method = env->GetStaticMethodID(environment, "isExternalStorageManager", "()Z")) {
            result = env->CallStaticBooleanMethod(environment, method);
        }
        env->DeleteLocalRef(environment);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    return result;
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

float RenderScale() {
    float scale = 0.667f;
    std::ifstream in(LogDir() / "render_scale.txt");
    std::string line;
    if (std::getline(in, line)) {
        line = Trim(line);
        char* end = nullptr;
        const float value = std::strtof(line.c_str(), &end);
        if (end != line.c_str()) scale = std::clamp(value, 0.25f, 1.0f);
    }
    LogInfo("android: render scale {:.3f}", scale);
    return scale;
}

namespace {

// a static method of the game's activity class (PTActivity), with the JNI env of this thread
template <typename Call>
bool CallActivityStatic(const char* name, const char* signature, Call&& call) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return false;
    jclass cls = env->GetObjectClass(activity);
    jmethodID method = env->GetStaticMethodID(cls, name, signature);
    bool ok = false;
    if (method) {
        call(env, cls, method);
        ok = !env->ExceptionCheck();
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return ok;
}

}  // namespace

void SetTouchControlsEnabled(bool enabled) {
    CallActivityStatic("setTouchControlsEnabled", "(Z)V",
                       [&](JNIEnv* env, jclass cls, jmethodID m) { env->CallStaticVoidMethod(cls, m, static_cast<jboolean>(enabled)); });
}

void SetSecondScreenMode(int mode) {
    CallActivityStatic("setSecondScreenMode", "(I)V",
                       [&](JNIEnv* env, jclass cls, jmethodID m) { env->CallStaticVoidMethod(cls, m, static_cast<jint>(mode)); });
}

bool HasSecondScreen() {
    jboolean has = JNI_FALSE;
    CallActivityStatic("hasSecondScreen", "()Z", [&](JNIEnv* env, jclass cls, jmethodID m) { has = env->CallStaticBooleanMethod(cls, m); });
    return has == JNI_TRUE;
}

void UpdateSecondScreen(int loop, const std::string& line) {
    static int last_loop = -1;
    static std::string last_line;
    if (loop == last_loop && line == last_line) return;
    last_loop = loop;
    last_line = line;
    CallActivityStatic("updateSecondScreen", "(I[B)V", [&](JNIEnv* env, jclass cls, jmethodID m) {
        // UTF-8 bytes, decoded in Java: NewStringUTF wants modified UTF-8 and rejects some valid text
        jbyteArray bytes = env->NewByteArray(static_cast<jsize>(line.size()));
        if (!bytes) return;
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(line.size()), reinterpret_cast<const jbyte*>(line.data()));
        env->CallStaticVoidMethod(cls, m, static_cast<jint>(loop), bytes);
        env->DeleteLocalRef(bytes);
    });
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

// driver.txt: in Download/PT-logs (reachable with a file manager), else in the app's own folder
std::string DriverChoice() {
    for (const std::filesystem::path& file : {LogDir() / "driver.txt", DataDir() / "driver.txt"}) {
        std::ifstream in(file);
        std::string line;
        if (std::getline(in, line) && !Trim(line).empty()) {
            line = Trim(line);
            LogInfo("android: driver.txt in {}: {}", file.parent_path().string(), line);
            return line;
        }
    }
    return "turnip";
}

PFN_vkGetInstanceProcAddr OpenVulkanDriver(std::string& name) {
    const std::string choice = DriverChoice();
    std::string lower = choice;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "system") {
        name = "system driver (driver.txt)";
        return nullptr;
    }
    const std::string lib_dir = NativeLibraryDir();
    if (lib_dir.empty()) {
        LogWarn("android: nativeLibraryDir unknown, using the system driver");
        name = "system driver (no library folder)";
        return nullptr;
    }
    const std::string hook_dir = lib_dir + "/";
    std::string driver_dir = hook_dir;
    std::string driver_file = "libvulkan_turnip.so";
    std::string label = "Mesa Turnip (bundled)";
    if (lower != "turnip") {
        /* another driver: a .so (the one inside an AdrenoTools driver zip) named in driver.txt and put next to it. A library
           cannot be loaded from shared storage, so it is copied into the app's private folder first. */
        const std::filesystem::path source = LogDir() / choice;
        const std::filesystem::path private_dir = std::filesystem::path(SDL_GetAndroidInternalStoragePath()) / "drivers";
        std::error_code ec;
        std::filesystem::create_directories(private_dir, ec);
        const std::filesystem::path target = private_dir / source.filename();
        if (!std::filesystem::is_regular_file(source, ec) ||
            !std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec)) {
            LogWarn("android: driver {} not found or not copied ({}), using the bundled Turnip", source.string(), ec.message());
        } else {
            driver_dir = private_dir.string() + "/";
            driver_file = source.filename().string();
            label = "custom driver " + driver_file;
        }
    }
    const char* cache = SDL_GetAndroidCachePath();
    void* handle = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, cache, hook_dir.c_str(), driver_dir.c_str(),
                                              driver_file.c_str(), nullptr, nullptr);
    if (!handle) {
        const char* error = dlerror();
        LogWarn("android: libadrenotools could not load {} ({}), using the system driver", label, error ? error : "no error text");
        name = "system driver (" + label + " failed to load)";
        return nullptr;
    }
    auto proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(handle, "vkGetInstanceProcAddr"));
    if (!proc) {
        LogWarn("android: {} has no vkGetInstanceProcAddr, using the system driver", label);
        name = "system driver (" + label + " incomplete)";
        return nullptr;
    }
    name = label;
    return proc;
}

}  // namespace

namespace {
bool g_system_driver_forced = false;
}

void ForceSystemVulkanDriver() { g_system_driver_forced = true; }
bool SystemVulkanDriverForced() { return g_system_driver_forced; }

PFN_vkGetInstanceProcAddr LoadVulkanDriver(std::string& name) {
    // opened once per process: a second libadrenotools load would map the driver again
    static std::string chosen;
    static const PFN_vkGetInstanceProcAddr proc = OpenVulkanDriver(chosen);
    name = chosen;
    return proc;
}

namespace {

// what the overlay sent last (UI thread), applied on the game thread
std::atomic<uint32_t> g_touch_buttons{0};
std::atomic<uint64_t> g_touch_axes{0}; // left x, left y, right x, right y as four Sint16
std::atomic<bool> g_touch_used{false};
std::atomic<bool> g_touch_visible{false}; // the overlay is up (TouchControls.nativeVisible)
SDL_Joystick* g_touch_pad = nullptr;

constexpr uint32_t kTouchA = 1, kTouchB = 2, kTouchStart = 4, kTouchZoom = 8, kTouchName = 16;

int16_t Axis(uint64_t packed, int index) { return static_cast<int16_t>((packed >> (16 * index)) & 0xFFFF); }

}  // namespace

void NoteControllerEvent(const SDL_Event& event) {
    if (!g_touch_visible.load(std::memory_order_relaxed)) return;
    SDL_JoystickID which = 0;
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        which = event.gbutton.which;
    } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && (event.gaxis.value > 16384 || event.gaxis.value < -16384)) {
        which = event.gaxis.which;
    } else {
        return;
    }
    if (SDL_IsJoystickVirtual(which)) return;
    g_touch_visible = false;
    LogInfo("android: controller {} used, touch controls put away", which);
    CallActivityStatic("controllerUsed", "()V", [](JNIEnv* env, jclass cls, jmethodID m) { env->CallStaticVoidMethod(cls, m); });
}

void PumpTouchControls() {
    if (!g_touch_used.load(std::memory_order_relaxed)) return;
    if (!g_touch_pad) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "Touch controls";
        const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        if (!id || !(g_touch_pad = SDL_OpenJoystick(id))) {
            LogError("android: touch controls: no virtual gamepad ({})", SDL_GetError());
            g_touch_used = false;
            return;
        }
        LogInfo("android: touch controls attached as virtual gamepad {}", id);
    }
    const uint64_t axes = g_touch_axes.load(std::memory_order_relaxed);
    const uint32_t buttons = g_touch_buttons.load(std::memory_order_relaxed);
    SDL_SetJoystickVirtualAxis(g_touch_pad, SDL_GAMEPAD_AXIS_LEFTX, Axis(axes, 0));
    SDL_SetJoystickVirtualAxis(g_touch_pad, SDL_GAMEPAD_AXIS_LEFTY, Axis(axes, 1));
    SDL_SetJoystickVirtualAxis(g_touch_pad, SDL_GAMEPAD_AXIS_RIGHTX, Axis(axes, 2));
    SDL_SetJoystickVirtualAxis(g_touch_pad, SDL_GAMEPAD_AXIS_RIGHTY, Axis(axes, 3));
    const int16_t trigger = (buttons & kTouchName) ? 32767 : -32768;
    SDL_SetJoystickVirtualAxis(g_touch_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, trigger);
    SDL_SetJoystickVirtualAxis(g_touch_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, trigger);
    SDL_SetJoystickVirtualButton(g_touch_pad, SDL_GAMEPAD_BUTTON_SOUTH, (buttons & kTouchA) != 0);
    SDL_SetJoystickVirtualButton(g_touch_pad, SDL_GAMEPAD_BUTTON_EAST, (buttons & kTouchB) != 0);
    SDL_SetJoystickVirtualButton(g_touch_pad, SDL_GAMEPAD_BUTTON_START, (buttons & kTouchStart) != 0);
    SDL_SetJoystickVirtualButton(g_touch_pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK, (buttons & kTouchZoom) != 0);
}

}  // namespace pt::android

extern "C" JNIEXPORT void JNICALL Java_com_ptport_app_TouchControls_nativeState(JNIEnv*, jclass, jfloat lx, jfloat ly, jfloat rx,
                                                                                 jfloat ry, jint buttons) {
    auto to16 = [](float v) {
        return static_cast<uint64_t>(static_cast<uint16_t>(static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f)));
    };
    pt::android::g_touch_axes.store(to16(lx) | (to16(ly) << 16) | (to16(rx) << 32) | (to16(ry) << 48), std::memory_order_relaxed);
    pt::android::g_touch_buttons.store(static_cast<uint32_t>(buttons), std::memory_order_relaxed);
    pt::android::g_touch_used.store(true, std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL Java_com_ptport_app_TouchControls_nativeVisible(JNIEnv*, jclass, jboolean visible) {
    pt::android::g_touch_visible.store(visible, std::memory_order_relaxed);
}


#endif
