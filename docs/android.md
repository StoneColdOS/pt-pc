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

- `Download/PT-logs/pt.log`: the log, with `pt-crash.txt` (and `pt-java-crash.txt`) after a crash. Send these when something
  goes wrong; `adb pull /storage/emulated/0/Download/PT-logs` copies them to a computer. The same lines go to logcat
  under the tag `pt`. Without All files access they are in `Android/data/com.ptport.app/files/data/` instead.
- `Download/PT-logs/render_scale.txt`: the render size against the screen, `0.667` by default (`0.5` faster, `1` sharpest).
- `Android/data/com.ptport.app/files/data/pt.ini`: settings and saves, as on PC.
- `Download/PT-logs/driver.txt`: the Vulkan driver. `turnip` (default, the bundled Mesa Turnip), `system` (the device's
  own Qualcomm driver), or the file name of another driver's `.so` (from an AdrenoTools driver zip) put in the same folder.

## Performance

On the AYN Thor (Snapdragon 8 Gen 2, Adreno 740) at the default render scale the game holds about 60 fps in the start room
and 45-60 in the hallway.

## Controls

A built-in or connected gamepad works as the PS4 pad. The voice part (saying "Jack") is done by holding both triggers.

## Known gaps

- No speech recognition yet; use the trigger chord.
- Switching away from the app while playing may lose the window and crash; save first.
- The PC upscalers, ray tracing, VR, enhanced textures and loop previews are not available.

## Building

`.github/workflows/android.yml` builds the APK on every push to `android-port`. Locally: `tools/android/build_native.sh` with
`ANDROID_NDK_HOME` set, then `./gradlew assembleRelease` in `android/`.
