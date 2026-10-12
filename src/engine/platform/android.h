#pragma once

#include <SDL3/SDL_events.h>
// Android (docs/android.md): where the game keeps its files, the data bundled in the APK, and the Vulkan driver.
#ifdef __ANDROID__

#include <filesystem>
#include <string>

#include "engine/render/vk.h"

namespace pt::android {

/* The app's folder on shared storage, Android/data/<package>/files: the game files (game/CUSA01127), pt.ini, saves and
   pt.log live here, so they can be copied to and from the device over USB without root. */
std::filesystem::path DataDir();

/* Whether the app may read the shared Download folder (Android's "All files access", MANAGE_EXTERNAL_STORAGE). */
bool HasAllFilesAccess();

/* Where pt.log and pt-crash.txt go: Download/PT-logs when the app has All files access (reachable with any file manager or
   LocalSend), otherwise DataDir()/data. */
std::filesystem::path LogDir();

/* The scene's render size against the screen: 0.667 unless LogDir()/render_scale.txt holds another (0.25 to 1). */
/* How much of the screen's resolution the game renders at (the compositor scales it up for free). The setting
   (display.android_resolution) picks a share or Auto, which goes by the GPU: 0.667 on the fast Adreno parts (730 and up,
   the 8xx ones from 825), 0.58 in the middle, 0.5 on the rest. render_scale.txt in Download/PT-logs still wins when present. */
float RenderScale(const char* gpu_name);
/* The setting: 0 Auto, then kResolutionShares[1..]. A change rebuilds the swapchain at the next frame (main.cpp). */
inline constexpr float kResolutionShares[] = {0.0f, 0.5f, 0.58f, 0.667f, 0.75f, 0.85f, 1.0f};
inline constexpr int kResolutionChoices = static_cast<int>(sizeof(kResolutionShares) / sizeof(kResolutionShares[0]));
void SetResolutionSetting(int index);

/* The second screen of a dual-screen handheld (AYN Thor and others; android/.../SecondScreen.java): the loop the player is on
   and the subtitle line playing now (empty for none), sent on only when one of them changes. Does nothing without one. */
void UpdateSecondScreen(int loop, const std::string& line);

/* What the second screen shows (AppSettings display.second_screen): 0 clock and subtitles, 1 clock, 2 dim clock, 3 off. Nothing
   is shown there until this is called once. */
void SetSecondScreenMode(int mode);

/* Whether this device has a second screen to draw on (the settings page offers the choice only then). */
bool HasSecondScreen();

/* Writes the signal, the fault address and a backtrace (library + offset per frame) to `path` when the game crashes. */
void InstallCrashLog(const std::filesystem::path& path);

/* Copies the shaders and fonts packed in the APK's assets (the list in pt_assets.txt) into DataDir() when this APK's
   list differs from the one copied last time. The engine reads them as ordinary files from there. */
bool UnpackBundledAssets();

/* The Vulkan driver chosen in DataDir()/driver.txt: "turnip" (the default, Mesa Turnip bundled in the APK and loaded
   through libadrenotools) or "system" (the device's own driver). Returns the driver's vkGetInstanceProcAddr, or null for
   the system loader; `name` says which one was taken. */
PFN_vkGetInstanceProcAddr LoadVulkanDriver(std::string& name);
/* The bundled driver found no GPU it runs (an Adreno newer than Turnip knows, such as the 8xx): the context retries with
   the system's driver (vk_context.cpp), and LoadVulkanDriver is not asked again. */
void ForceSystemVulkanDriver();
/* Settings and saves: DataDir()/data, or the app's internal storage when that folder cannot be written (a file manager or
   an earlier install made it under another owner, and Android then refuses the app its own folder). */
std::filesystem::path UserDataDir();
/* The APK's versionName (v0.2.1, or test-<run>-<commit> for a test build) and the device ("AYN Thor, Android 14"), for the
   top of pt.log: the game's own version line is the PC port's. */
std::string AppVersion();
std::string DeviceName();
bool SystemVulkanDriverForced();

/* The on-screen touch controls (android/.../TouchControls.java) as a virtual SDL gamepad, which the game reads like any
   other pad: attached on the first touch, its sticks and buttons set from what the overlay last sent. Called once a frame
   before the events are pumped. */
void PumpTouchControls();
/* The touch controls setting (AppSettings display.touch_controls): off keeps the overlay away whatever is touched. */
void SetTouchControlsEnabled(bool enabled);
/* An SDL event from a real (not the touch controls') gamepad: a pressed button or a stick or trigger past half puts the
   touch controls away. The game sees every pad here, also the ones SDL reads itself (HIDAPI) that Android's input events
   never report, such as the AYN Thor's built-in controls. */
void NoteControllerEvent(const SDL_Event& event);

}  // namespace pt::android

#endif
