#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/common.sh"
cd "$(project_root)"

expected_remote=https://github.com/evc24004/dobby.git
actual_remote=$(git remote get-url origin 2>/dev/null || true)
[ "$actual_remote" = "$expected_remote" ] || {
    echo "error: origin is '$actual_remote', expected '$expected_remote'" >&2
    exit 1
}
[ "$(git branch --show-current)" = main ] || {
    echo "error: publishing is only allowed from main" >&2
    exit 1
}

git add -A
scripts/verify-public.sh --staged

if ! git diff --cached --quiet; then
    git commit -m "${1:-Update Dobby developer client}"
else
    echo "No source changes to commit."
fi
git push origin main
