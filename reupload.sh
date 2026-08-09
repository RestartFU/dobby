#!/bin/sh
set -eu

expected_remote="https://github.com/evc24004/dobby.git"

cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "error: run the initial repository setup before using this script" >&2
    exit 1
fi

actual_remote=$(git remote get-url origin 2>/dev/null || true)
if [ "$actual_remote" != "$expected_remote" ]; then
    echo "error: origin is '$actual_remote', expected '$expected_remote'" >&2
    exit 1
fi

git add -A

staged_files=$(git diff --cached --name-only --diff-filter=ACMR)
if [ -z "$staged_files" ]; then
    echo "No changes to upload."
    exit 0
fi

for forbidden in \
    'crash.txt' \
    'packet-debugger.log' \
    'latest-violation.txt' \
    'last-copied-diagnostic.txt'; do
    if printf '%s\n' "$staged_files" | grep -Fx "$forbidden" >/dev/null; then
        echo "error: refusing to publish private runtime artifact: $forbidden" >&2
        exit 1
    fi
done

if git diff --cached --check; then :; else
    echo "error: staged changes contain whitespace errors" >&2
    exit 1
fi

scan_pathspec=':(exclude)reupload.sh'
secret_pattern='github_pat_|gh[pousr]_[A-Za-z0-9_]{20,}|AKIA[0-9A-Z]{16}|BEGIN [A-Z ]*PRIVATE KEY|Authorization:[[:space:]]*(Bearer|Basic)|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}'
if git grep --cached -n -I -E "$secret_pattern" -- . "$scan_pathspec"; then
    echo "error: possible credential, private key, authorization header, or email found" >&2
    exit 1
fi

if git grep --cached -n -I -E '/Users/[^/[:space:]]+|/home/[^/[:space:]]+' -- . "$scan_pathspec"; then
    echo "error: personal absolute home path found" >&2
    exit 1
fi

if [ -n "${HOME:-}" ] && git grep --cached -n -I -F "$HOME" -- . "$scan_pathspec"; then
    echo "error: current home directory found in staged content" >&2
    exit 1
fi

cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host
ctest --test-dir build-host --output-on-failure

message=${1:-"Update packet debugger"}
git commit -m "$message"
git push origin main
