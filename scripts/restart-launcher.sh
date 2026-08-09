#!/bin/sh
set -eu

bundle_id=io.mrarm.mcpelauncher.ui
launcher_name='Minecraft Bedrock Launcher'
log_path="$HOME/Library/Application Support/mcpelauncher/dobby.log"
before_lines=0
[ ! -f "$log_path" ] || before_lines=$(wc -l < "$log_path" | tr -d ' ')

osascript -e "tell application id \"$bundle_id\" to quit" >/dev/null 2>&1 || true
open -b "$bundle_id"

clicked=0
attempt=0
while [ "$attempt" -lt 45 ]; do
    if osascript \
        -e 'tell application "System Events"' \
        -e "tell first application process whose bundle identifier is \"$bundle_id\"" \
        -e 'if exists button "PLAY" of window 1 then' \
        -e 'click button "PLAY" of window 1' \
        -e 'return "clicked"' \
        -e 'end if' \
        -e 'end tell' \
        -e 'end tell' 2>/dev/null | grep -q clicked; then
        clicked=1
        break
    fi
    sleep 1
    attempt=$((attempt + 1))
done

if [ "$clicked" -ne 1 ]; then
    echo "warning: $launcher_name opened, but PLAY could not be clicked automatically" >&2
    exit 0
fi

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
