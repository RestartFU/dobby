#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/common.sh"
cd "$(project_root)"

run_sanitizers=1
android_abi=${DOBBY_ANDROID_ABI:-$(default_android_abi)}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --skip-sanitizers) run_sanitizers=0 ;;
        --abi)
            shift
            [ "$#" -gt 0 ] || { echo "error: --abi requires a value" >&2; exit 2; }
            android_abi=$1
            ;;
        *)
            echo "error: unknown build option: $1" >&2
            exit 2
            ;;
    esac
    shift
done
validate_android_abi "$android_abi"

jobs=$(parallel_jobs)
toolchain=$(resolve_ndk_toolchain)
android_build=$(android_build_dir "$android_abi")

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

cmake -S . -B "$android_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DANDROID_ABI="$android_abi" \
    -DANDROID_PLATFORM=android-23
cmake --build "$android_build" --target dobby --parallel "$jobs"

artifact="$android_build/libdobby.so"
[ -s "$artifact" ] || { echo "error: Android artifact was not produced" >&2; exit 1; }
case "$android_abi" in
    arm64-v8a) artifact_pattern='ARM aarch64|arm64' ;;
    x86_64) artifact_pattern='x86-64|x86_64' ;;
esac
file "$artifact" | grep -Eq "$artifact_pattern" || {
    echo "error: Android artifact does not match $android_abi" >&2
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

sed "0,/\"arch\": \"[^\"]*\"/s//\"arch\": \"$android_abi\"/" \
    mod.json > "$android_build/mod.json"
printf '%s\n' "$android_abi" > "$android_build/abi.txt"
printf 'Built %s (%s)\n' "$(sha256_file "$artifact")" "$android_abi"
