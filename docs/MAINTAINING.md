# Maintaining P.T. for Android

A one-page guide for keeping this port going: how releases work, what to protect, how to read a player's log, and the
fixes we already know. Written at v0.2.6 (October 2026).

## The project in one paragraph

This repository (StoneColdOS/pt-pc, branch `android-port`) is an Android build of LoreanXavier's native PC port of P.T.
(github.com/LoreanXavier/pt-pc, used with their permission). The game code is theirs; this fork adds the Android layer:
the APK, the graphics drivers, the setup from a player's own .pkg, touch controls, the microphone, and the AYN Thor's
second screen. No game files are ever included.

## Where things live

| What | Where |
| --- | --- |
| Android Java code (setup screen, second screen, touch controls, driver import) | `android/app/src/main/java/com/ptport/app/` |
| Android native code (drivers, settings glue, logs) | `src/engine/platform/android.cpp` and `.h` |
| The .pkg extractor | `src/pkginstall/` |
| Build script (downloads drivers, packs the APK) | `tools/android/build_native.sh` |
| Automatic build and release | `.github/workflows/android.yml` |
| Release number | `android/release-version.txt` |
| Release notes shown on GitHub | `docs/android-release-notes.md` |
| Player-facing instructions | `README.md` |
| Technical notes | `docs/android.md` |

## Putting out a release

1. Every push to `android-port` builds a **test APK** and posts it as the **android-latest** release. Use that for your own
   testing.
2. To publish a numbered release: put the new number in `android/release-version.txt` (for example `v0.2.7`), add a
   "What's new" section at the top of `docs/android-release-notes.md`, and push. The build publishes
   `android-v0.2.7` with `PT-Android-v0.2.7.apk` by itself.
3. Check it on the Thor before telling people. A bad release cannot be rolled back: Android refuses to install an older
   version over a newer one, so a broken release is fixed by a quick next release.

Tip: testers often install the numbered release even when asked to use android-latest. If a fix only touches a group of
devices that is already broken, shipping it as a numbered release is usually the faster way to get it tested.

## The signing key (protect this)

Every APK since v0.2.1 is signed with one permanent key. That is what lets updates install over the old version and keep
players' game files and saves.

- The key is stored as two GitHub secrets on the repository: `PT_KEYSTORE_B64` and `PT_KEYSTORE_PASSWORD`
  (Settings > Secrets and variables > Actions).
- A backup copy is `pt-release.jks` plus its password, in `Downloads/pt_20200926/signing-key` on the Mac. **Keep a second
  backup somewhere safe** (password manager or iCloud).
- If the key is ever lost, every player has to uninstall and set the game up again. Never put the key in the repository.
- Its SHA-256 fingerprint: `C9:32:8F:05:35:CE:9C:62:99:43:E3:40:66:FC:4A:DE:AB:A4:D9:EC:88:FE:CD:3A:7E:DC:0E:52:1F:74:FE:A6`.
  If the build log ever warns that `PT_KEYSTORE_B64` is not set, the secrets are missing.

## Reading a player's pt.log

Players find logs in **Download/PT-logs**: `pt.log` is the latest run, `pt-previous.log` the one before. The lines that
matter, in order:

| Line starts with | What it tells you |
| --- | --- |
| `android: app v0.2.6 on Xiaomi 2312DRA50G, SoC SM7435` | Their app version and device. If it is old, ask them to update first. |
| `vulkan: driver:` | Which graphics driver was tried (bundled Turnip, the device's own, imported, or a fallback). |
| `vulkan: using ... Adreno (TM) 710` | Their GPU. Mali, PowerVR or Xclipse means unsupported for now. |
| `vulkan: ... skipped: Vulkan 1.1 (1.3 needed)` | That driver is too old for the game; the app falls back by itself. |
| `android: render scale 0.500 (auto on ...)` | The resolution in use. |
| `status: 20 fps, gpu 48 ms` | Speed every 10 seconds. A high gpu number means the graphics chip is the limit; lower Resolution. |
| `error` or `device lost` | Something broke. "device lost" usually means a driver problem: try another driver. |

## Known problems and their fixes

| What the player sees | Cause | Fix |
| --- | --- | --- |
| "The graphics device could not be set up" | No driver can run their GPU | Adreno only. Try Settings > Display > Graphics driver, or import a Turnip .zip. The launcher offers this after a failed start. |
| Green flickering blocks, or "device lost" | Driver bug on their chip | Another driver: Device's own, or an imported one. The Adreno 710/720/722 already get a fixed driver automatically. |
| Slow | Weak GPU | Settings > Display > Resolution 50%. Optional: Settings > Graphics > Image clarity to sharpen. |
| "P.T. cannot prepare its data folder" | Old files from an earlier install | Fixed in v0.2.4 (falls back to private storage). If it persists: Settings > Apps > P.T. > Clear storage. |
| Update won't install | They have v0.2.0 or older (different signature) | Uninstall once, reinstall, pick the .pkg again. |
| Clock appears on a monitor or TV | Pre-v0.2.2 | Update; only built-in screens are used now. |
| Name at the end not heard | Mic permission or accent | Settings > Microphone test; holding both triggers always works. |

## Graphics drivers

- **Bundled Turnip (KIMCHI v26.0.0 R8)**: the default for most Adreno 6xx/7xx, including the Thor's 740.
- **Turnip for Adreno 710/720/722 (Vauzi-17 v4.2)**: picked automatically on those GPUs (7s Gen 2, 6 Gen 1, 6 Gen 3,
  7 Gen 3, 7 Gen 4). Source: github.com/Vauzi-17/710. Mesa is MIT; keep the credit in the README.
- **Device's own**: used automatically for Adreno 8xx, where the bundled ones do not run.
- **Imported**: any AdrenoTools .zip via Settings > Display > Import a driver. PurpleVK was about 35% lighter on the Thor's
  GPU in testing and could become the Thor's default after a longer test.
- If a driver fails, the game tries the next one by itself; if all fail, the launcher shows a recovery screen.

## Open threads

- **Mali support**: pull request #1 from lectrontechnologies (pt-mali fork) is stage 1 of 6 for Vulkan 1.1 Mali GPUs.
  Hold off merging until it can draw a frame, or until it is behind an opt-in setting.
- **Upstream**: a few fixes would help the PC version too (radio subtitles going blank on repeats, sticks in the pause
  menu's language list, B backing out of the pause menu). They could be offered to LoreanXavier as a pull request.
- **Ideas not done on purpose**: frame generation (adds lag, smears in a dark grainy game), FSR 1 (costs GPU on the phones
  that need it most; Image clarity does the cheap part), custom touch layouts (later, if asked).

## Picking this up with Claude

Start a session on this repository and say: "Read docs/MAINTAINING.md, then here is a player's log." That file plus the
log is usually enough to find the problem.
