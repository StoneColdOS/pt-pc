# Android

An experimental arm64 build for Snapdragon (Adreno 6xx/7xx) devices, developed for the AYN Thor. It is the same game as the PC
port, built as `libmain.so` for SDL's Android activity, with Mesa Turnip bundled as the Vulkan driver.

## Installing

1. Download `PT-Android.apk` from the `android-latest` release of this fork and install it (allow installs from your browser
   or file manager when Android asks).
2. Put `chunk1.psarc`, `texture.qar` and `pathid_list_ps4.bin` from your own CUSA01127 dump in the device's `Download`
   folder, loose or in a `CUSA01127` folder. LocalSend's default location works.
3. Start P.T. When Android asks, allow it "All files access" (needed to read the Download folder), then start P.T. again.

The app's own folder, `Android/data/com.ptport.app/files/game/CUSA01127/`, also works and needs no permission, but most
file managers cannot write there on Android 11 and newer.

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
