#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/common.sh"
cd "$(project_root)"

android_abi=${DOBBY_ANDROID_ABI:-$(default_android_abi)}
if [ "${1:-}" = "--abi" ]; then
    shift
    [ "$#" -gt 0 ] || { echo "error: --abi requires a value" >&2; exit 2; }
    android_abi=$1
    shift
fi
[ "$#" -eq 0 ] || { echo "error: unknown install option: $1" >&2; exit 2; }
validate_android_abi "$android_abi"
android_build=$(android_build_dir "$android_abi")
artifact="$android_build/libdobby.so"
manifest="$android_build/mod.json"
[ -s "$artifact" ] || { echo "error: run the Android build first" >&2; exit 1; }
[ -s "$manifest" ] || { echo "error: generated mod manifest is missing" >&2; exit 1; }

launcher_root=$(launcher_root)
mods_root="$launcher_root/mods"
profiles="$launcher_root/profiles/profiles.ini"

selected_profile=${DOBBY_LAUNCHER_PROFILE:-}
if [ -z "$selected_profile" ] && [ -f "$profiles" ]; then
    selected_profile=$(awk '
        $0 == "[General]" {general=1; next}
        /^\[/ {general=0}
        general && /^selected=/ {sub(/^selected=/, ""); print; exit}
    ' "$profiles")
fi
if [ -z "$selected_profile" ] && [ -f "$profiles" ]; then
    selected_profile=$(sed -n 's/^\[\([^]]*\)\]$/\1/p' "$profiles" |
        awk '$0 != "General" {print; exit}')
fi

registered=""
if [ -f "$profiles" ]; then
    registered=$(awk -v profile="$selected_profile" -v abi="$android_abi" '
        $0 == "[" profile "]" {selected=1; next}
        /^\[/ {selected=0}
        selected && /^mods\\[0-9][0-9]*\\path=/ {
            path=$0
            sub(/^mods\\[0-9][0-9]*\\path=/, "", path)
            if (path ~ "/(dobby|packet-debugger)/" && path ~ "/" abi "/?$")
                registered=path
        }
        END {print registered}
    ' "$profiles")
fi
if [ "$(host_platform)" = linux ]; then
    mod_version=$(python3 -c '
import json, sys
with open(sys.argv[1], encoding="utf-8") as source:
    print(json.load(source)["version"]["version"])
' "$manifest")
    install_dir="$mods_root/Dobby/$mod_version/$android_abi/"
else
    install_dir=${registered:-"$mods_root/dobby/1.26.40.5/$android_abi/"}
fi

case "$install_dir" in
    "$mods_root"/*) ;;
    *) echo "error: refusing launcher path outside the mod directory: $install_dir" >&2; exit 1 ;;
esac

other_libraries=$(find "$install_dir" -maxdepth 1 -type f -name '*.so' ! -name libdobby.so -print 2>/dev/null || true)
if [ -n "$other_libraries" ]; then
    echo "error: another mod library occupies the selected Dobby slot:" >&2
    printf '%s\n' "$other_libraries" >&2
    exit 1
fi

mkdir -p "$install_dir"
if [ -f "$install_dir/libdobby.so" ] && ! cmp -s "$artifact" "$install_dir/libdobby.so"; then
    backup_dir="$launcher_root/disabled-mods/dobby-deploy-backups/$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$backup_dir"
    cp "$install_dir/libdobby.so" "$backup_dir/libdobby.so"
    [ ! -f "$install_dir/mod.json" ] || cp "$install_dir/mod.json" "$backup_dir/mod.json"
    echo "Previous build backed up to: $backup_dir"
fi

artifact_temp="$install_dir/.libdobby.so.$$"
manifest_temp="$install_dir/.mod.json.$$"
trap 'rm -f "$artifact_temp" "$manifest_temp"' EXIT HUP INT TERM
cp "$artifact" "$artifact_temp"
cp "$manifest" "$manifest_temp"
mv "$artifact_temp" "$install_dir/libdobby.so"
mv "$manifest_temp" "$install_dir/mod.json"
trap - EXIT HUP INT TERM

build_hash=$(sha256_file "$artifact")
installed_hash=$(sha256_file "$install_dir/libdobby.so")
[ "$build_hash" = "$installed_hash" ] || { echo "error: installed artifact hash mismatch" >&2; exit 1; }

printf '%s\n' "$install_dir" > "$android_build/install-path.txt"
echo "Installed Dobby $build_hash to: $install_dir"

if [ -f "$profiles" ] && [ -n "$selected_profile" ]; then
    python3 scripts/update-profile-mods.py \
        --profiles "$profiles" \
        --profile "$selected_profile" \
        --mod-path "$install_dir" \
        --backup-root "$launcher_root/disabled-mods/dobby-profile-backups"
fi

legacy_linux_root="$mods_root/dobby"
if [ "$(host_platform)" = linux ] && [ -d "$legacy_linux_root" ]; then
    layout_backup="$launcher_root/disabled-mods/dobby-layout-backups/$(date +%Y%m%d-%H%M%S)-$$"
    mkdir -p "$layout_backup"
    mv "$legacy_linux_root" "$layout_backup/dobby"
    echo "Redundant Linux Dobby layout moved to: $layout_backup/dobby"
fi

if [ -d capes ]; then
    echo "==> Validating and installing local persona cape packs"
    python3 scripts/install-capes.py --source capes --launcher-root "$launcher_root"
else
    echo "Local capes directory not present; skipped cape-pack installation."
fi
