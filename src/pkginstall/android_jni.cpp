// libptsetup.so: the extractor (pkg_extract.h) for the Android setup screen (android/.../SetupActivity.java). Kept apart from
// libmain.so so the setup screen does not load the game or SDL.
#include <android/log.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "pkginstall/pkg_extract.h"

namespace {

std::string Utf8(JNIEnv* env, jstring text) {
    if (!text) return {};
    const char* chars = env->GetStringUTFChars(text, nullptr);
    std::string out = chars ? chars : "";
    env->ReleaseStringUTFChars(text, chars);
    return out;
}

}  // namespace

// String nativeExtract(int fd, String destination): null when done, else the message for the player. Calls back
// boolean onExtractProgress(long done, long total, String stage) on the same object; false cancels.
extern "C" JNIEXPORT jstring JNICALL Java_com_ptport_app_SetupActivity_nativeExtract(JNIEnv* env, jobject self, jint fd, jstring destination) {
    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        return env->NewStringUTF("Could not open the chosen file. Copy the .pkg onto the device itself (not a cloud folder) and try again.");
    }
    // a pipe from a cloud provider cannot be read at random: the package has to be a real file on the device
    if (lseek(fd, 0, SEEK_CUR) < 0) {
        return env->NewStringUTF("This file cannot be read directly. Copy the .pkg onto the device itself (not a cloud folder) and try again.");
    }
    pt::pkg::Source source;
    source.size = static_cast<uint64_t>(st.st_size);
    source.read = [fd](uint64_t offset, void* buffer, size_t count) {
        auto* out = static_cast<char*>(buffer);
        while (count > 0) {
            const ssize_t got = pread(fd, out, count, static_cast<off_t>(offset));
            if (got <= 0) return false;
            out += got;
            offset += static_cast<uint64_t>(got);
            count -= static_cast<size_t>(got);
        }
        return true;
    };
    jclass cls = env->GetObjectClass(self);
    jmethodID on_progress = env->GetMethodID(cls, "onExtractProgress", "(JJLjava/lang/String;)Z");
    std::string last_stage;
    jstring stage_text = nullptr;
    const auto result = pt::pkg::ExtractPt(source, Utf8(env, destination), [&](const pt::pkg::Progress& p) {
        if (!on_progress) return true;
        if (p.stage != last_stage || !stage_text) {
            if (stage_text) env->DeleteLocalRef(stage_text);
            stage_text = env->NewStringUTF(p.stage.c_str());
            last_stage = p.stage;
        }
        const bool keep_going = env->CallBooleanMethod(self, on_progress, static_cast<jlong>(p.done), static_cast<jlong>(p.total), stage_text);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        return keep_going;
    });
    if (stage_text) env->DeleteLocalRef(stage_text);
    env->DeleteLocalRef(cls);
    __android_log_print(result.ok ? ANDROID_LOG_INFO : ANDROID_LOG_WARN, "pt", "setup: %s %s (%s)", result.ok ? "installed" : "failed",
                        result.content_id.c_str(), result.ok ? "ok" : result.error.c_str());
    for (const auto& w : result.warnings) __android_log_print(ANDROID_LOG_WARN, "pt", "setup: %s", w.c_str());
    if (result.ok) return nullptr;
    return env->NewStringUTF(result.cancelled ? "" : result.error.c_str());
}
