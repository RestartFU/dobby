#!/bin/sh
set -eu

bundle_id=io.mrarm.mcpelauncher.ui
profiles_path="$HOME/Library/Application Support/mcpelauncher/profiles/profiles.ini"
log_path="$HOME/Library/Application Support/mcpelauncher/dobby.log"
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
[ -n "$profile" ] || {
    echo "error: no launcher profile selected; set DOBBY_LAUNCHER_PROFILE" >&2
    exit 1
}

osascript -e "tell application id \"$bundle_id\" to quit" >/dev/null 2>&1 || true
sleep 1
open -n -b "$bundle_id" --args --profile "$profile"
echo "Started launcher profile: $profile"

attempt=0
while [ "$attempt" -lt 45 ]; do
    if [ -f "$log_path" ]; then
        new_log=$(tail -n "+$((before_lines + 1))" "$log_path")
        if printf '%s\n' "$new_log" | grep -q 'READY: Dobby'; then
            printf '%s\n' "$new_log" | grep -E \
                'library loaded: Dobby|installed ReadOnlyBinaryStream|installed PacketViolationWarningPacket|READY: Dobby|registered Mods > Dobby'
            exit 0
        fi
    fi
    sleep 1
    attempt=$((attempt + 1))
done

echo "error: Minecraft started, but Dobby did not report READY within 45 seconds" >&2
exit 1
