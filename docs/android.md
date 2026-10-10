# Android

An experimental arm64 build for Snapdragon (Adreno 6xx/7xx) devices, developed for the AYN Thor. It is the same game as the PC
port, built as `libmain.so` for SDL's Android activity, with Mesa Turnip bundled as the Vulkan driver.

## Installing

1. Download `PT-Android.apk` from the `android-latest` release of this fork and install it (allow installs from your browser
   or file manager when Android asks).
2. Start P.T. once and close it. That creates `Android/data/com.ptport.app/files/`.
3. Copy `chunk1.psarc`, `texture.qar` and `pathid_list_ps4.bin` from your own CUSA01127 dump into
   `Android/data/com.ptport.app/files/game/CUSA01127/`. A PC over USB is the easiest way.
4. Start P.T. again.

## Files

Everything lives in `Android/data/com.ptport.app/files/`:

- `data/pt.log`: the log. Send this when something goes wrong. The same lines also go to logcat under the tag `pt`.
- `data/pt.ini`: settings, as on PC.
- `driver.txt`: `turnip` (default, the bundled Mesa Turnip) or `system` (the device's own Adreno driver).
- `shaders/`, `fonts/`: unpacked from the APK on the first start of each new build.

## Controls

A built-in or connected gamepad works as the PS4 pad. The voice part (saying "Jack") is done by holding both triggers.

## Known gaps

- No speech recognition yet; use the trigger chord.
- Switching away from the app while playing may lose the window and crash; save first.
- The PC upscalers, ray tracing, VR, enhanced textures and loop previews are not available.

## Building

`.github/workflows/android.yml` builds the APK on every push to `android-port`. Locally: `tools/android/build_native.sh` with
`ANDROID_NDK_HOME` set, then `./gradlew assembleRelease` in `android/`.
