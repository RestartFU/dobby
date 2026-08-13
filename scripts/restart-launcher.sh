#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/common.sh"

platform=$(host_platform)
launcher_root=$(launcher_root)
profiles_path="$launcher_root/profiles/profiles.ini"
log_path="$launcher_root/dobby.log"
before_lines=0
[ ! -f "$log_path" ] || before_lines=$(wc -l < "$log_path" | tr -d ' ')

profile=${DOBBY_LAUNCHER_PROFILE:-}
if [ -z "$profile" ] && [ -f "$profiles_path" ]; then
    profile=$(awk '
        $0 == "[General]" {general=1; next}
        /^\[/ {general=0}
        general && /^selected=/ {sub(/^selected=/, ""); print; exit}
    ' "$profiles_path")
fi
if [ -z "$profile" ] && [ -f "$profiles_path" ]; then
    profile=$(sed -n 's/^\[\([^]]*\)\]$/\1/p' "$profiles_path" |
        awk '$0 != "General" {print; exit}')
fi
[ -n "$profile" ] || {
    echo "error: no launcher profile selected; set DOBBY_LAUNCHER_PROFILE" >&2
    exit 1
}

case "$platform" in
    macos)
        bundle_id=io.mrarm.mcpelauncher.ui
        client_pattern='^/Applications/Minecraft Bedrock Launcher.app/Contents/MacOS/(\./)?mcpelauncher-client-arm64-v8a '
        osascript -e "tell application id \"$bundle_id\" to quit" >/dev/null 2>&1 || true
        pkill -TERM -f "$client_pattern" 2>/dev/null || true
        attempt=0
        while pgrep -f "$client_pattern" >/dev/null 2>&1 && [ "$attempt" -lt 10 ]; do
            sleep 1
            attempt=$((attempt + 1))
        done
        if pgrep -f "$client_pattern" >/dev/null 2>&1; then
            echo "error: existing Minecraft client did not stop cleanly" >&2
            exit 1
        fi
        open -n -b "$bundle_id" --args --profile "$profile"
        ;;
    linux)
        app_id=io.mrarm.mcpelauncher
        command -v flatpak >/dev/null 2>&1 || {
            echo "error: Linux launch currently requires the mcpelauncher Flatpak" >&2
            exit 1
        }
        flatpak info "$app_id" >/dev/null 2>&1 || {
            echo "error: $app_id is not installed" >&2
            exit 1
        }
        flatpak kill "$app_id" >/dev/null 2>&1 || true
        flatpak run "$app_id" --profile "$profile" >/dev/null 2>&1 &
        client_pattern='mcpelauncher-client'
        ;;
esac

echo "Started launcher profile: $profile"

attempt=0
while [ "$attempt" -lt 45 ]; do
    if [ -f "$log_path" ]; then
        new_log=$(tail -n "+$((before_lines + 1))" "$log_path")
        if printf '%s\n' "$new_log" | grep -q 'READY: Dobby'; then
            client_pid=$(pgrep -f "$client_pattern" | tail -1)
            [ -n "$client_pid" ] || {
                echo "error: Dobby reported READY but the Minecraft client already exited" >&2
                exit 1
            }
            stable=0
            while [ "$stable" -lt 10 ]; do
                kill -0 "$client_pid" 2>/dev/null || {
                    echo "error: Minecraft crashed during the Dobby stability check" >&2
                    exit 1
                }
                sleep 1
                stable=$((stable + 1))
            done
            printf '%s\n' "$new_log" | grep -E \
                'library loaded: Dobby|installed ReadOnlyBinaryStream|installed PacketViolationWarningPacket|READY: Dobby|registered Mods > Dobby'
            echo "Minecraft client $client_pid remained stable for 10 seconds after READY."
            exit 0
        fi
    fi
    sleep 1
    attempt=$((attempt + 1))
done

echo "error: Minecraft started, but Dobby did not report READY within 45 seconds" >&2
exit 1
