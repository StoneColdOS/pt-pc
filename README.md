# P.T. for Android (experimental)

An unofficial Android build of **[LoreanXavier's native PC port of P.T.](https://github.com/LoreanXavier/pt-pc)**, for
Snapdragon handhelds and phones. It runs the same native code as the PC port, not an emulator, and reads everything from
your own copy of the game.

**All of the real work is LoreanXavier's.** They rebuilt P.T. in C++ and wrote the Vulkan renderer. This fork only adds the
Android layer: the APK, the Snapdragon driver setup, storage, screen rotation and handheld tuning. If you enjoy this,
please support the original project ([Patreon](https://patreon.com/loreanxavier)). Their original README is in
[README_UPSTREAM.md](README_UPSTREAM.md).

> This is an early test build, developed and tested on **one device**, an AYN Thor Max. Expect rough edges and save
> often. It contains **no game files**, and nothing here helps you get them.

## Will it run on my device?

| | |
| --- | --- |
| **GPU** | Qualcomm **Adreno 6xx or 7xx** (Snapdragon). Tested on the Adreno 740 (Snapdragon 8 Gen 2) only. |
| **Android** | 10 or newer, 64-bit |
| **Not supported** | Mali, Immortalis, Samsung Xclipse, PowerVR, Google Tensor, MediaTek. The game needs a texture format those GPUs lack. |
| **Controller** | A built-in or connected gamepad. There are no touch controls. |

On the AYN Thor it runs at about 60 fps in the start room and roughly 25 to 35 fps in the heavier later loops.

## What you need

Your own dump of P.T. (**CUSA01127**, the US release), or rather three files from it:

- `chunk1.psarc`
- `texture.qar`
- `pathid_list_ps4.bin`

If you have a fake PKG made from your dump rather than a folder, the original port's PC installer (Windows or Linux) can
pull these three files out of it. A store PKG will not work.

## Installing

1. Download the latest `PT-Android-*.apk` from [Releases](../../releases) and install it.
2. Copy the three game files into your device's **Download** folder (loose, or in a `CUSA01127` folder inside it).
   LocalSend, a USB cable or any file manager works.
3. Start P.T. When Android asks, allow it **All files access**, which it needs to read the Download folder. Then start it again.

## Controls

The Xbox-layout controls of the PS4 original. The part where you say the name is done by **holding both triggers**
(no microphone support yet).

## When something goes wrong

The app writes its log to **Download/PT-logs**: `pt.log`, plus `pt-crash.txt` after a crash. Attach those when you report
a problem, with your device and chip. From a computer, `adb pull /storage/emulated/0/Download/PT-logs` grabs them.

Two optional files in that folder for tinkering:

- `render_scale.txt`: the render size against the screen, `0.667` by default. `0.5` is faster, `1` is sharpest.
- `driver.txt`: `turnip` (default, the bundled Mesa Turnip driver), `system` (your device's own driver), or the file name
  of another driver's `.so` placed in the same folder.

## Known issues

- No microphone for the voice part (use the triggers).
- The PC extras that need a desktop (upscalers, ray tracing, VR, enhanced textures, loop previews) are off.
- The second screen of dual-screen handhelds is not used.

## Building

Every push to the `android-port` branch builds the APK on GitHub Actions (`.github/workflows/android.yml`). Locally:
`tools/android/build_native.sh` with `ANDROID_NDK_HOME` set, then `./gradlew assembleRelease` in `android/`. More detail
in [docs/android.md](docs/android.md).

## Credits

- [P.T. PC port](https://github.com/LoreanXavier/pt-pc) by LoreanXavier: the game itself, as native C++ and Vulkan.
- [Mesa Turnip](https://docs.mesa3d.org/drivers/freedreno.html), the open-source Adreno Vulkan driver, in the
  [KIMCHI AdrenoTools build](https://github.com/K11MCH1/AdrenoToolsDrivers).
- [libadrenotools](https://github.com/bylaws/libadrenotools) by bylaws, which loads that driver in an app.
- [SDL3](https://github.com/libsdl-org/SDL) for the Android activity, input and audio.
- [LibertyRecompAndroid](https://github.com/vaduur/LibertyRecompAndroid), whose GTA IV port showed the way for Turnip on
  Android handhelds.

P.T. is the work of Kojima Productions and is owned by Konami. This project is not affiliated with, endorsed by or
connected to either of them, or to LoreanXavier. The port's own code is under the MIT license (see LICENSE).
