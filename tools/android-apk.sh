#!/usr/bin/env bash
# FEAT-097: builds the Android app's APK on this computer (Windows, Git Bash), signs it with a local
# test key and, with --install, installs it on the phone connected by USB (adb).
#
#   tools/android-apk.sh [arm64|x86_64] [--install]
#
# arm64 (phones) is the default; x86_64 is the emulator. The APK goes to build/android-apk/.
#
# Needs (Qt 6.12's Android page, https://doc.qt.io/qt-6.12/android.html: NDK 27.2.12479018, JDK 21 or
# newer, platforms;android-36, build-tools;36.0.0):
#   - Qt 6.12.0 MinGW (the host) and Qt 6.12.0 for Android under QT_ROOT (default C:/Qt);
#   - the Android SDK under ANDROID_SDK_ROOT (default %LOCALAPPDATA%/Android/Sdk) with that NDK,
#     platform and build tools;
#   - a JDK under JAVA_HOME (default: Android Studio's).
# Every path can be changed with the variables below.
#
# The test key is made once in %LOCALAPPDATA%/Mentis/ and never enters the repository. It is not the
# key of another build machine: Android refuses to install one APK over another signed with a
# different key, and removing the app first also removes the maps kept in its storage.
set -euo pipefail

ARCH=arm64
INSTALL=0
for arg in "$@"; do
    case "$arg" in
        arm64) ARCH=arm64 ;;
        x86_64) ARCH=x86_64 ;;
        --install) INSTALL=1 ;;
        *) echo "Usage: $0 [arm64|x86_64] [--install]" >&2; exit 2 ;;
    esac
done
case "$ARCH" in
    arm64) QT_ABI=android_arm64_v8a ;;
    x86_64) QT_ABI=android_x86_64 ;;
esac

QT_VERSION="${QT_VERSION:-6.12.0}"
QT_ROOT="${QT_ROOT:-C:/Qt}"
QT_HOST="$QT_ROOT/$QT_VERSION/mingw_64"
QT_ANDROID="$QT_ROOT/$QT_VERSION/$QT_ABI"
QT_TOOLS="${QT_TOOLS:-$QT_ROOT/Tools}"
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}"
ANDROID_NDK_VERSION="${ANDROID_NDK_VERSION:-27.2.12479018}"
ANDROID_BUILD_TOOLS="${ANDROID_BUILD_TOOLS:-36.0.0}"
JAVA_HOME="${JAVA_HOME_FOR_ANDROID:-C:/Program Files/Android/Android Studio/jbr}"
KEY_DIR="$LOCALAPPDATA/Mentis"
KEYSTORE="$KEY_DIR/mentis-test.keystore"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/android-$ARCH"
HOST_BUILD="$ROOT/build/android-host"
OUT="$ROOT/build/android-apk"

missing() { echo "Missing: $1" >&2; exit 1; }
[ -x "$QT_HOST/bin/qmake.exe" ] || missing "Qt $QT_VERSION MinGW at $QT_HOST"
[ -f "$QT_ANDROID/bin/qt-cmake.bat" ] || missing "Qt $QT_VERSION for Android at $QT_ANDROID"
[ -d "$ANDROID_SDK_ROOT/ndk/$ANDROID_NDK_VERSION" ] || missing "NDK $ANDROID_NDK_VERSION in $ANDROID_SDK_ROOT/ndk"
[ -d "$ANDROID_SDK_ROOT/build-tools/$ANDROID_BUILD_TOOLS" ] || missing "build-tools $ANDROID_BUILD_TOOLS in $ANDROID_SDK_ROOT"
[ -x "$JAVA_HOME/bin/java.exe" ] || missing "a JDK at $JAVA_HOME (set JAVA_HOME_FOR_ANDROID)"

export JAVA_HOME
# PATH takes Unix paths: "C:/..." would be split at its colon.
for dir in "$QT_HOST/bin" "$QT_TOOLS/mingw1310_64/bin" "$QT_TOOLS/Ninja" "$QT_TOOLS/CMake_64/bin" "$JAVA_HOME/bin"; do
    PATH="$(cygpath -u "$dir"):$PATH"
done
export PATH

# The translations tool runs at build time, so it is built for this computer first.
TRANSLATIONS="$HOST_BUILD/tools/translations/MentisTranslations.exe"
if [ ! -x "$TRANSLATIONS" ]; then
    cmake -S "$ROOT/tools/translations" -B "$HOST_BUILD" -G Ninja -DCMAKE_PREFIX_PATH="$QT_HOST"
    cmake --build "$HOST_BUILD"
fi

# Configured once; later runs only build what changed.
if [ ! -f "$BUILD/build.ninja" ]; then
    "$QT_ANDROID/bin/qt-cmake.bat" -S "$ROOT" -B "$BUILD" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
        -DANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/$ANDROID_NDK_VERSION" \
        -DQT_HOST_PATH="$QT_HOST" \
        -DMENTIS_HOST_TRANSLATIONS="$TRANSLATIONS" \
        -DBUILD_TESTING=OFF
fi
cmake --build "$BUILD" --target MentisMobile_make_apk

# Qt's release APK is unsigned, and Android refuses an unsigned package.
TOOLS="$ANDROID_SDK_ROOT/build-tools/$ANDROID_BUILD_TOOLS"
if [ ! -f "$KEYSTORE" ]; then
    mkdir -p "$KEY_DIR"
    keytool -genkeypair -keystore "$KEYSTORE" -storepass mentis-test -keypass mentis-test -alias mentis-test \
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Mentis local test build" -noprompt
fi
UNSIGNED=$(find "$BUILD" -name "*.apk" -path "*outputs/apk/*" | head -n 1)
[ -n "$UNSIGNED" ] || { echo "No APK found under $BUILD" >&2; exit 1; }
mkdir -p "$OUT"
COMMIT=$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo local)
APK="$OUT/MentisMobile-$QT_ABI-$COMMIT.apk"
"$TOOLS/zipalign.exe" -f -p 4 "$UNSIGNED" "$OUT/aligned.apk"
"$TOOLS/apksigner.bat" sign --ks "$KEYSTORE" --ks-pass pass:mentis-test --key-pass pass:mentis-test --out "$APK" "$OUT/aligned.apk"
rm -f "$OUT/aligned.apk" "$APK.idsig"
echo "APK: $APK"

if [ "$INSTALL" = 1 ]; then
    ADB="$ANDROID_SDK_ROOT/platform-tools/adb.exe"
    # -r keeps the app's data (its maps). An APK signed with another key does not install over it.
    # adb is a Windows program: with MSYS_NO_PATHCONV it needs the Windows path.
    if ! MSYS_NO_PATHCONV=1 "$ADB" install -r "$(cygpath -w "$APK")"; then
        echo "Not installed. If the app on the phone was signed with another key (an APK of another build" >&2
        echo "machine), Android refuses it: removing the app first also removes the maps in its storage." >&2
        exit 1
    fi
fi
