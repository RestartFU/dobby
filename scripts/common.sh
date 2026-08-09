#!/bin/sh

project_root() {
    CDPATH= cd -- "$(dirname -- "$0")/.." && pwd
}

parallel_jobs() {
    jobs=$(sysctl -n hw.logicalcpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
    case "$jobs" in
        ''|*[!0-9]*) echo 4 ;;
        *) echo "$jobs" ;;
    esac
}

resolve_ndk_toolchain() {
    for ndk_root in "${ANDROID_NDK_HOME:-}" "${ANDROID_NDK_ROOT:-}"; do
        if [ -n "$ndk_root" ] && [ -f "$ndk_root/build/cmake/android.toolchain.cmake" ]; then
            printf '%s\n' "$ndk_root/build/cmake/android.toolchain.cmake"
            return 0
        fi
    done

    cached_toolchain=$(sed -n 's/^CMAKE_TOOLCHAIN_FILE:[^=]*=//p' \
        build-android-arm64/CMakeCache.txt 2>/dev/null | head -1)
    if [ -n "$cached_toolchain" ] && [ -f "$cached_toolchain" ]; then
        printf '%s\n' "$cached_toolchain"
        return 0
    fi

    for candidate in \
        /opt/homebrew/share/android-commandlinetools/ndk/*/build/cmake/android.toolchain.cmake \
        "$HOME/Library/Android/sdk/ndk"/*/build/cmake/android.toolchain.cmake; do
        if [ -f "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    echo "error: Android NDK not found; set ANDROID_NDK_HOME" >&2
    return 1
}
