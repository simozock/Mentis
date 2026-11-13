#!/usr/bin/env bash
# FEAT-097: runs the Android app on the phone connected by USB, building it first only when needed.
#
#   tools/android-run.sh [--build]
#
# 1. Looks for an arm64 phone that adb can use (USB debugging on and this computer authorised).
#    ANDROID_SERIAL picks one when several are connected.
# 2. Uses the newest APK in build/android-apk/ when no source file is newer than it; otherwise (or
#    with --build) builds a new one with tools/android-apk.sh.
# 3. Installs it, keeping the app's data, unless that same APK is already on that phone, then
#    starts the app.
#
# Run it from Git Bash or the Terminal panel, not from an agent's tools: Gradle fails there (see
# tools/android-apk.sh). Paths can be changed with the same variables as tools/android-apk.sh.
set -euo pipefail

FORCE_BUILD=0
for arg in "$@"; do
    case "$arg" in
        --build) FORCE_BUILD=1 ;;
        *) echo "Usage: $0 [--build]" >&2; exit 2 ;;
    esac
done

PACKAGE=org.qtproject.example.MentisMobile
ACTIVITY=org.qtproject.qt.android.bindings.QtActivity
ABI=arm64-v8a
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/android-apk"
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}"
ADB_EXE="$ANDROID_SDK_ROOT/platform-tools/adb.exe"
[ -x "$ADB_EXE" ] || { echo "Missing: adb at $ADB_EXE" >&2; exit 1; }
# adb is a Windows program: with MSYS_NO_PATHCONV it takes Windows paths unchanged.
adb() { MSYS_NO_PATHCONV=1 "$ADB_EXE" "$@"; }

# --- 1. The phone -----------------------------------------------------------------------------
# Emulators ("emulator-5554") and adb over Wi-Fi ("host:port") are left out: only USB devices.
SERIAL=""
NOT_READY=""
while read -r serial state _; do
    [ -n "$serial" ] || continue
    case "$serial" in emulator-*|*:*) continue ;; esac
    if [ -n "${ANDROID_SERIAL:-}" ] && [ "$serial" != "$ANDROID_SERIAL" ]; then continue; fi
    if [ "$state" != device ]; then NOT_READY="$NOT_READY $serial($state)"; continue; fi
    abis=$(adb -s "$serial" shell getprop ro.product.cpu.abilist | tr -d '\r')
    case ",$abis," in
        *",$ABI,"*) SERIAL="$serial"; break ;;
        *) NOT_READY="$NOT_READY $serial(no $ABI: $abis)" ;;
    esac
done < <(adb devices | tail -n +2 | tr -d '\r')

if [ -z "$SERIAL" ]; then
    echo "No compatible phone connected by USB." >&2
    [ -z "$NOT_READY" ] || echo "Found but not usable:$NOT_READY" >&2
    echo "Connect the phone, turn on USB debugging and accept 'Allow USB debugging' on its screen." >&2
    exit 1
fi
MODEL=$(adb -s "$SERIAL" shell getprop ro.product.model | tr -d '\r')
echo "Phone: $MODEL ($SERIAL)"

# --- 2. The APK -------------------------------------------------------------------------------
newest_apk() { ls -t "$OUT"/MentisMobile-android_arm64_v8a-*.apk 2>/dev/null | head -n 1 || true; }

APK=$(newest_apk)
STALE=""
if [ "$FORCE_BUILD" = 1 ]; then
    STALE="--build"
elif [ -z "$APK" ]; then
    STALE="no APK yet"
else
    # Only what goes into the APK: documentation, tests and the other tools do not.
    NEWER=$(cd "$ROOT" && find CMakeLists.txt resources.qrc assets mobile src third_party translations \
        tools/translations -type f -newer "$APK" ! -name '*.md' -print -quit)
    [ -z "$NEWER" ] || STALE="$NEWER changed after the last build"
fi

if [ -n "$STALE" ]; then
    echo "Building ($STALE)..."
    "$ROOT/tools/android-apk.sh" arm64
    APK=$(newest_apk)
else
    echo "Up to date: $(basename "$APK")"
fi

# --- 3. Install and start ---------------------------------------------------------------------
# The stamp records the APK last installed on each phone, so an unchanged APK is not sent again.
STAMP="$OUT/.installed-$SERIAL"
APK_ID="$(basename "$APK") $(stat -c %Y "$APK")"
INSTALLED=$(adb -s "$SERIAL" shell pm list packages "$PACKAGE" | tr -d '\r' | grep -x "package:$PACKAGE" || true)
if [ -n "$INSTALLED" ] && [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$APK_ID" ]; then
    echo "Already installed on the phone."
else
    echo "Installing..."
    # -r keeps the app's data (its maps). An APK signed with another key does not install over it.
    if ! adb -s "$SERIAL" install -r "$(cygpath -w "$APK")"; then
        echo "Not installed. If the app on the phone was signed with another key (an APK of another build" >&2
        echo "machine), Android refuses it: removing the app first also removes the maps in its storage." >&2
        exit 1
    fi
    echo "$APK_ID" > "$STAMP"
fi

# A fresh start, so the phone shows this build and not a copy still in memory.
adb -s "$SERIAL" shell am force-stop "$PACKAGE"
adb -s "$SERIAL" shell am start -n "$PACKAGE/$ACTIVITY" > /dev/null
echo "Started $PACKAGE on $MODEL."
