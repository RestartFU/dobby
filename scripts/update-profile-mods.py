#!/usr/bin/env python3
"""Register one Dobby path and remove stale Dobby entries from a launcher profile."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import tempfile
import time


MOD_PATH = re.compile(r"^mods\\([0-9]+)\\path=(.*?)(?:\r?\n)?$")
MOD_SIZE = re.compile(r"^mods\\size=(?:[0-9]+)(?:\r?\n)?$")


def is_dobby_path(value: str) -> bool:
    parts = {part.casefold() for part in Path(value).parts}
    return "dobby" in parts or "packet-debugger" in parts


def updated_lines(lines: list[str], profile: str, dobby_path: str) -> list[str]:
    header = f"[{profile}]"
    start = next(
        (index for index, line in enumerate(lines) if line.rstrip("\r\n") == header),
        None,
    )
    if start is None:
        raise ValueError(f"launcher profile does not exist: {profile}")
    end = next(
        (
            index
            for index in range(start + 1, len(lines))
            if lines[index].startswith("[")
        ),
        len(lines),
    )

    section = lines[start + 1 : end]
    retained_paths: list[str] = []
    body: list[str] = []
    insertion = None
    for line in section:
        path_match = MOD_PATH.match(line)
        if path_match:
            if insertion is None:
                insertion = len(body)
            value = path_match.group(2)
            if not is_dobby_path(value):
                retained_paths.append(value)
            continue
        if MOD_SIZE.match(line):
            if insertion is None:
                insertion = len(body)
            continue
        body.append(line)

    retained_paths.append(dobby_path)
    generated = [
        f"mods\\{index}\\path={value}\n"
        for index, value in enumerate(retained_paths, start=1)
    ]
    generated.append(f"mods\\size={len(retained_paths)}\n")
    position = len(body) if insertion is None else insertion
    body[position:position] = generated
    return lines[: start + 1] + body + lines[end:]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profiles", required=True, type=Path)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--mod-path", required=True)
    parser.add_argument("--backup-root", required=True, type=Path)
    args = parser.parse_args()

    original = args.profiles.read_text(encoding="utf-8").splitlines(keepends=True)
    changed = updated_lines(original, args.profile, args.mod_path)
    if changed == original:
        print(f"Dobby is already enabled once for launcher profile: {args.profile}")
        return 0

    args.backup_root.mkdir(parents=True, exist_ok=True)
    backup = args.backup_root / (
        f"profiles.{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}.ini"
    )
    shutil.copy2(args.profiles, backup)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{args.profiles.name}.dobby-", dir=args.profiles.parent
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as temporary:
            temporary.writelines(changed)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.chmod(temporary_name, args.profiles.stat().st_mode)
        os.replace(temporary_name, args.profiles)
    finally:
        if os.path.exists(temporary_name):
            os.unlink(temporary_name)

    print(f"Enabled one Dobby entry for launcher profile: {args.profile}")
    print(f"Previous profile configuration backed up to: {backup}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
