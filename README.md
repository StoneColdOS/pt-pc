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
| **GPU** | Qualcomm **Adreno 6xx or 7xx** (Snapdragon). Tested on the Adreno 740 (Snapdragon 8 Gen 2) only. Adreno 8xx (Snapdragon 8 Elite, 7s Gen 4 and newer) falls back to the device's own driver, untested so far. |
| **Android** | 10 or newer, 64-bit |
| **Not supported** | Mali, Immortalis, Samsung Xclipse, PowerVR, Google Tensor, MediaTek. The game needs a texture format those GPUs lack. |
| **Controller** | A built-in or connected gamepad, or the on-screen touch controls. |

On the AYN Thor it runs at about 60 fps in the start room and roughly 25 to 35 fps in the heavier later loops.

## What you need

Your own copy of P.T. (**CUSA01127**, the US release; the European and Japanese releases should work too) as one of:

- **a fake PKG (`.pkg`) made from your dump.** The app takes the game files out of it by itself. If it came zipped
  (`.rar`, `.zip`, `.7z`), unpack it first with a file manager such as ZArchiver.
- **the three files from a dump folder:** `chunk1.psarc`, `texture.qar` and `pathid_list_ps4.bin`.

A store (PlayStation Store) PKG will not work: it is encrypted for the console that owns it, and this app does not touch that.

## Installing

1. Download the latest `PT-Android-*.apk` from [Releases](../../releases) and install it.
2. Start P.T. and choose **Choose your P.T. .pkg**, then pick your package in Android's file picker. Setting up takes a
   minute or two and needs about 1.4 GB free. Afterwards you can let the app delete the `.pkg` to get that space back.
3. That's it: from then on the game starts straight away.

If you have the three extracted files instead, choose **I already have the extracted files**, copy them into your Download
folder (loose or in a `CUSA01127` folder), and allow **All files access** when asked.

## Controls

**Touch controls:** without a controller, on-screen controls show up: drag on the left half to walk, on the right half to
look, and the buttons for Use, Zoom, Back, Pause and Name (the voice part). They also appear whenever you touch the screen,
and fade out after a few seconds without a touch or as soon as a controller is used. To turn them off for good:
**Display > Touch controls > Off**.

**Controller:** The Xbox-layout controls of the PS4 original. Near the end the game asks you to say a name: say it out loud and the game
hears it through the microphone, or **hold both triggers** instead.

The app asks for the microphone once, before the first start. The speech recognizer (whisper.cpp) runs on the device and
only listens in that last part; nothing is recorded or sent anywhere, and the app has no internet permission. You can test
the microphone in the game's settings.

## When something goes wrong

The app writes its log to **Download/PT-logs**: `pt.log`, plus `pt-crash.txt` after a crash. Attach those when you report
a problem, with your device and chip. From a computer, `adb pull /storage/emulated/0/Download/PT-logs` grabs them.

Two optional files in that folder for tinkering:

- `render_scale.txt`: overrides the **Display > Resolution** setting (a number like `0.5`). The setting is easier: Auto
  picks a value for your GPU, and 50% is the fastest choice.
- `driver.txt`: `turnip` (default, the bundled Mesa Turnip driver), `system` (your device's own driver), or the file name
  of another driver's `.so` placed in the same folder.

## Dual-screen handhelds

On an AYN Thor (or a similar handheld with a second screen) the bottom screen shows the hallway's clock, frozen at 23:59,
with the loop you're on, and the radio and other subtitle lines as they play (the whole broadcast, even the lines the
main screen skips on repeat loops, with *static* marking where the voice drops out). Change it in the game's settings under
**Display > Second screen**: clock and subtitles, clock only, dim clock, or off.

## Known issues

- The speech recognizer is new on Android: if it doesn't hear you, hold both triggers.
- The PC extras that need a desktop (upscalers, ray tracing, VR, enhanced textures, loop previews) are off.

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
- [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (MIT) with OpenAI's Whisper base.en model (MIT) and the Silero VAD
  (MIT), which hear the name at the end.
- [Special Elite](https://fonts.google.com/specimen/Special+Elite) by Astigmatic (Apache-2.0), the typewriter face of the
  icon and the second screen.
- [LibertyRecompAndroid](https://github.com/vaduur/LibertyRecompAndroid), whose GTA IV port showed the way for Turnip on
  Android handhelds.

P.T. is the work of Kojima Productions and is owned by Konami. This project is not affiliated with, endorsed by or
connected to either of them, or to LoreanXavier. The port's own code is under the MIT license (see LICENSE).
