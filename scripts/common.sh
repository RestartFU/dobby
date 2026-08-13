#!/bin/sh

project_root() {
    CDPATH= cd -- "$(dirname -- "$0")/.." && pwd
}

host_platform() {
    case "$(uname -s)" in
        Darwin) printf '%s\n' macos ;;
        Linux) printf '%s\n' linux ;;
        *)
            echo "error: unsupported host platform: $(uname -s)" >&2
            return 1
            ;;
    esac
}

default_android_abi() {
    case "$(uname -m)" in
        arm64|aarch64) printf '%s\n' arm64-v8a ;;
        x86_64|amd64) printf '%s\n' x86_64 ;;
        *)
            echo "error: unsupported host architecture: $(uname -m)" >&2
            return 1
            ;;
    esac
}

validate_android_abi() {
    case "$1" in
        arm64-v8a|x86_64) ;;
        *)
            echo "error: unsupported Android ABI: $1 (expected arm64-v8a or x86_64)" >&2
            return 1
            ;;
    esac
}

android_build_dir() {
    case "$1" in
        arm64-v8a) printf '%s\n' build-android-arm64 ;;
        x86_64) printf '%s\n' build-android-x86_64 ;;
        *) validate_android_abi "$1" ;;
    esac
}

launcher_root() {
    if [ -n "${DOBBY_LAUNCHER_ROOT:-}" ]; then
        printf '%s\n' "$DOBBY_LAUNCHER_ROOT"
        return
    fi
    case "$(host_platform)" in
        macos)
            printf '%s\n' "$HOME/Library/Application Support/mcpelauncher"
            ;;
        linux)
            flatpak_root="$HOME/.var/app/io.mrarm.mcpelauncher/data/mcpelauncher"
            native_root="${XDG_DATA_HOME:-$HOME/.local/share}/mcpelauncher"
            if [ -d "$flatpak_root" ] || [ ! -d "$native_root" ]; then
                printf '%s\n' "$flatpak_root"
            else
                printf '%s\n' "$native_root"
            fi
            ;;
    esac
}

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
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

    for cache in build-android-*/CMakeCache.txt; do
        [ -f "$cache" ] || continue
        cached_toolchain=$(sed -n 's/^CMAKE_TOOLCHAIN_FILE:[^=]*=//p' \
            "$cache" 2>/dev/null | head -1)
        if [ -n "$cached_toolchain" ] && [ -f "$cached_toolchain" ]; then
            printf '%s\n' "$cached_toolchain"
            return 0
        fi
    done

    for candidate in \
        /opt/homebrew/share/android-commandlinetools/ndk/*/build/cmake/android.toolchain.cmake \
        "$HOME/Library/Android/sdk/ndk"/*/build/cmake/android.toolchain.cmake \
        "$HOME/Android/Sdk/ndk"/*/build/cmake/android.toolchain.cmake; do
        if [ -f "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    echo "error: Android NDK not found; set ANDROID_NDK_HOME" >&2
    return 1
}
