#!/usr/bin/env bash
# Builds the Android native part (docs/android.md) and lays it out for Gradle:
#   android/app/libs/arm64-v8a/  libmain.so, libSDL3.so, libadrenotools' hooks, the Turnip driver
#   android/app/assets/          the compiled shaders, the fonts and pt_assets.txt (what the game unpacks on first start)
# Needs ANDROID_NDK_HOME (NDK r27 or newer), cmake 3.28+, ninja and glslc on the PATH.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build/android}"
APP="$ROOT/android/app"
NDK="${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to the NDK folder}"
GLSLC="${GLSLC:-$(command -v glslc)}"

# Mesa Turnip for Adreno 6xx/7xx (MIT), the KIMCHI build packaged for libadrenotools
TURNIP_URL="https://github.com/K11MCH1/AdrenoToolsDrivers/releases/download/v26.0.0-rc08/Turnip_v26.0.0_R8.zip"
TURNIP_SHA256="e634db0f929e2205e95511c769071817d0390180ec72c8e690bc76375e813715"

cmake -G Ninja -S "$ROOT" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release \
  -DVulkan_GLSLC_EXECUTABLE="$GLSLC" \
  -DPT_UPSCALERS=OFF -DPT_OPENXR=OFF
cmake --build "$BUILD" --target pt pt_shaders

LIBS="$APP/libs/arm64-v8a"
rm -rf "$LIBS" "$APP/assets"
mkdir -p "$LIBS" "$APP/assets/shaders" "$APP/assets/fonts"

cp "$BUILD/libmain.so" "$LIBS/"
for lib in libSDL3.so libmain_hook.so libfile_redirect_hook.so libgsl_alloc_hook.so libhook_impl.so; do
  found="$(find "$BUILD" -name "$lib" -type f | head -n1)"
  if [ -z "$found" ]; then
    echo "missing $lib in $BUILD" >&2
    exit 1
  fi
  cp "$found" "$LIBS/"
done
"$NDK"/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip --strip-unneeded "$LIBS"/*.so

TURNIP_ZIP="$BUILD/turnip.zip"
if [ ! -f "$TURNIP_ZIP" ] || ! echo "$TURNIP_SHA256  $TURNIP_ZIP" | sha256sum -c --status; then
  curl -fsSL -o "$TURNIP_ZIP" "$TURNIP_URL"
fi
echo "$TURNIP_SHA256  $TURNIP_ZIP" | sha256sum -c
driver="$(unzip -p "$TURNIP_ZIP" meta.json | python3 -c 'import json,sys; print(json.load(sys.stdin)["libraryName"])')"
# the package manager only unpacks lib*.so; the game asks libadrenotools for this name (src/engine/platform/android.cpp)
unzip -p "$TURNIP_ZIP" "$driver" > "$LIBS/libvulkan_turnip.so"

cp "$BUILD"/shaders/*.spv "$APP/assets/shaders/"
cp "$ROOT"/assets/fonts/*.ttf "$ROOT"/assets/fonts/OFL-*.txt "$APP/assets/fonts/"
(
  cd "$APP/assets"
  {
    echo "# $(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo local) $(date -u +%Y%m%dT%H%M%SZ)"
    find shaders fonts -type f | sort
  } > pt_assets.txt
)
echo "native build laid out in $APP"
ls -la "$LIBS"
