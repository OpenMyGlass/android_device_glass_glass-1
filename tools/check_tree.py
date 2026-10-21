#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the device tree: copy sources exist, no binaries, license headers, sorted lists."""
import re
import sys
from pathlib import Path

MAX_SIZE = 65536
COPY_RE = re.compile(r"(\$\(LOCAL_PATH\)|device/glass/glass-1)/([^:\s]+):")
SPDX_PATHS = ("rootdir/", "tools/", "tests/")
SPDX_FILES = ("system.prop", "proprietary-files.txt")


def _files(root):
    for path in sorted(root.rglob("*")):
        rel = path.relative_to(root)
        if ".git" in rel.parts or not path.is_file():
            continue
        yield path, rel.as_posix()


def _check_list(text, rel):
    problems = []
    seen = set()
    prev = None
    for n, line in enumerate(text.splitlines(), 1):
        if line.startswith("#"):
            seen, prev = set(), None
            continue
        if not line.strip():
            continue
        if line in seen:
            problems.append(f"{rel}:{n}: duplicate line {line!r}")
        elif prev is not None and line < prev:
            problems.append(f"{rel}:{n}: out of order: {line!r} after {prev!r}")
        seen.add(line)
        prev = line
    return problems


def check(root):
    root = Path(root)
    problems = []
    for path, rel in _files(root):
        data = path.read_bytes()
        if len(data) > MAX_SIZE:
            problems.append(f"{rel}: larger than {MAX_SIZE} bytes")
            continue
        if b"\0" in data:
            problems.append(f"{rel}: binary file (contains NUL)")
            continue
        text = data.decode("utf-8", errors="replace")
        if rel.endswith(".mk"):
            for line in text.splitlines():
                if line.strip().startswith("#"):
                    continue
                for m in COPY_RE.finditer(line):
                    if not (root / m.group(2)).is_file():
                        problems.append(f"{rel}: copy source missing: {m.group(2)}")
        if rel.startswith(SPDX_PATHS) or rel in SPDX_FILES:
            head = "\n".join(text.splitlines()[:5])
            if "SPDX-License-Identifier" not in head:
                problems.append(f"{rel}: no SPDX-License-Identifier in first 5 lines")
        if rel == "proprietary-files.txt":
            problems.extend(_check_list(text, rel))
    return problems


def main(argv):
    root = Path(argv[1]) if len(argv) > 1 else Path(".")
    problems = check(root)
    for p in problems:
        print(p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
