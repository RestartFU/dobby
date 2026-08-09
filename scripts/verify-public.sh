#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/common.sh"
cd "$(project_root)"

mode=${1:---worktree}
case "$mode" in
    --worktree)
        git diff --check
        files=$(git ls-files --cached --others --exclude-standard | while IFS= read -r path; do
            [ -f "$path" ] && printf '%s\n' "$path"
        done)
        ;;
    --staged)
        git diff --cached --check
        files=$(git diff --cached --name-only --diff-filter=ACMR)
        ;;
    *)
        echo "error: expected --worktree or --staged" >&2
        exit 2
        ;;
esac

for forbidden in \
    crash.txt packet-debugger.log latest-violation.txt last-copied-diagnostic.txt \
    dobby.log dobby-events.jsonl latest-dobby-violation.txt dobby-clipboard.txt; do
    if printf '%s\n' "$files" | awk -F/ -v name="$forbidden" '$NF == name {found=1} END {exit !found}'; then
        echo "error: refusing to publish private runtime artifact: $forbidden" >&2
        exit 1
    fi
done

scan_files=$(printf '%s\n' "$files" | grep -Ev '^(scripts/verify-public\.sh)$' || true)
[ -n "$scan_files" ] || exit 0

secret_pattern='github_pat_|gh[pousr]_[A-Za-z0-9_]{20,}|AKIA[0-9A-Z]{16}|BEGIN [A-Z ]*PRIVATE KEY|Authorization:[[:space:]]*(Bearer|Basic)|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}'
home_pattern='/Users/[^/[:space:]]+|/home/[^/[:space:]]+'

if printf '%s\n' "$scan_files" | xargs grep -nIH -E "$secret_pattern"; then
    echo "error: possible credential, private key, authorization header, or email found" >&2
    exit 1
fi

if printf '%s\n' "$scan_files" | xargs grep -nIH -E "$home_pattern"; then
    echo "error: personal absolute home path found" >&2
    exit 1
fi

echo "Public-content audit passed."
