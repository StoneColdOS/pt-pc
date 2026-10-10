#pragma once
// Android (docs/android.md): where the game keeps its files, the data bundled in the APK, and the Vulkan driver.
#ifdef __ANDROID__

#include <filesystem>
#include <string>

#include "engine/render/vk.h"

namespace pt::android {

/* The app's folder on shared storage, Android/data/<package>/files: the game files (game/CUSA01127), pt.ini, saves and
   pt.log live here, so they can be copied to and from the device over USB without root. */
std::filesystem::path DataDir();

/* Where pt.log and pt-crash.txt go: Download/PT-logs when the app has All files access (reachable with any file manager or
   LocalSend), otherwise DataDir()/data. */
std::filesystem::path LogDir();

/* Writes the signal, the fault address and a backtrace (library + offset per frame) to `path` when the game crashes. */
void InstallCrashLog(const std::filesystem::path& path);

/* Copies the shaders and fonts packed in the APK's assets (the list in pt_assets.txt) into DataDir() when this APK's
   list differs from the one copied last time. The engine reads them as ordinary files from there. */
bool UnpackBundledAssets();

/* The Vulkan driver chosen in DataDir()/driver.txt: "turnip" (the default, Mesa Turnip bundled in the APK and loaded
   through libadrenotools) or "system" (the device's own driver). Returns the driver's vkGetInstanceProcAddr, or null for
   the system loader; `name` says which one was taken. */
PFN_vkGetInstanceProcAddr LoadVulkanDriver(std::string& name);

}  // namespace pt::android

#endif
