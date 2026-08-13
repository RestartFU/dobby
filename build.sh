#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$project_root"

run_install=1
run_launch=1
run_publish=1
run_sanitizers=1
android_abi=${DOBBY_ANDROID_ABI:-}
commit_message="Update Dobby developer client"

usage() {
    cat <<'EOF'
Usage: ./build.sh [options]

Builds, tests, audits, installs, launches, commits, and pushes Dobby.

Options:
  --local              Build and test only (no install, launch, or push)
  --no-install         Do not install the Android artifact
  --no-launch          Do not restart the launcher or start Minecraft
  --no-push            Do not commit or push repository changes
  --skip-sanitizers    Skip the ASan/UBSan test build
  --abi ABI            Build arm64-v8a or x86_64 (defaults to host architecture)
  --message TEXT       Commit message used when publishing
  -h, --help           Show this help
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --local)
            run_install=0
            run_launch=0
            run_publish=0
            ;;
        --no-install) run_install=0 ;;
        --no-launch) run_launch=0 ;;
        --no-push) run_publish=0 ;;
        --skip-sanitizers) run_sanitizers=0 ;;
        --abi)
            shift
            [ "$#" -gt 0 ] || { echo "error: --abi requires a value" >&2; exit 2; }
            android_abi=$1
            ;;
        --message)
            shift
            [ "$#" -gt 0 ] || { echo "error: --message requires text" >&2; exit 2; }
            commit_message=$1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [ "$run_launch" -eq 1 ] && [ "$run_install" -eq 0 ]; then
    echo "error: --no-install cannot be combined with launching" >&2
    exit 2
fi

echo "==> Building and testing Dobby"
build_args=
if [ "$run_sanitizers" -eq 1 ]; then
    build_args=
else
    build_args=--skip-sanitizers
fi
if [ -n "$android_abi" ]; then
    scripts/build-all.sh ${build_args:+$build_args} --abi "$android_abi"
else
    scripts/build-all.sh ${build_args:+$build_args}
fi

echo "==> Auditing public repository content"
scripts/verify-public.sh --worktree

if [ "$run_install" -eq 1 ]; then
    echo "==> Installing the verified Android artifact"
    if [ -n "$android_abi" ]; then
        scripts/install-launcher.sh --abi "$android_abi"
    else
        scripts/install-launcher.sh
    fi
fi

if [ "$run_publish" -eq 1 ]; then
    echo "==> Publishing source changes"
    scripts/publish.sh "$commit_message"
fi

if [ "$run_launch" -eq 1 ]; then
    echo "==> Restarting the launcher and starting Minecraft"
    scripts/restart-launcher.sh
fi

echo "==> Dobby workflow complete"
