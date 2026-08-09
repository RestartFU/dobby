#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/common.sh"
cd "$(project_root)"

run_sanitizers=1
if [ "${1:-}" = "--skip-sanitizers" ]; then
    run_sanitizers=0
elif [ "$#" -gt 0 ]; then
    echo "error: unknown build option: $1" >&2
    exit 2
fi

jobs=$(parallel_jobs)
toolchain=$(resolve_ndk_toolchain)

cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --parallel "$jobs"
ctest --test-dir build-host --output-on-failure

if [ "$run_sanitizers" -eq 1 ]; then
    cmake -S . -B build-host-asan \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    cmake --build build-host-asan --parallel "$jobs"
    ctest --test-dir build-host-asan --output-on-failure
fi

cmake -S . -B build-android-arm64 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-23
cmake --build build-android-arm64 --target dobby --parallel "$jobs"

artifact=build-android-arm64/libdobby.so
[ -s "$artifact" ] || { echo "error: Android artifact was not produced" >&2; exit 1; }
file "$artifact" | grep -Eq 'ARM aarch64|arm64' || {
    echo "error: Android artifact is not ARM64" >&2
    exit 1
}

readelf_tool=$(find "$(dirname "$(dirname "$(dirname "$toolchain")")")/toolchains/llvm/prebuilt" \
    -name llvm-readelf -perm -111 2>/dev/null | head -1)
if [ -n "$readelf_tool" ]; then
    exports=$($readelf_tool --dyn-syms --wide "$artifact" | \
        awk '$5 == "GLOBAL" && $7 != "UND" && $8 !~ /^(_init|_fini)$/ {print $8}' | \
        sed 's/@.*//' | sort -u)
    expected=$(printf '%s\n' mod_init mod_preinit)
    [ "$exports" = "$expected" ] || {
        echo "error: unexpected public exports:" >&2
        printf '%s\n' "$exports" >&2
        exit 1
    }
else
    echo "warning: llvm-readelf unavailable; export audit skipped" >&2
fi

printf 'Built %s\n' "$(shasum -a 256 "$artifact" | awk '{print $1}')"
