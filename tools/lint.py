#!/usr/bin/env python3
"""Format and lint the C code under Application/ with clang-format and clang-tidy.

The files under .clang-format-ignore (ST libraries and image data) are skipped
by both tools. clang-tidy reads build/Debug/compile_commands.json, so build the
Debug preset first.

Usage:
  tools/lint.py format   # rewrite files in place
  tools/lint.py check    # fail if any file is not formatted
  tools/lint.py tidy     # run clang-tidy
"""

import fnmatch
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPILE_DB = os.path.join(ROOT, "build", "Debug")


def ignored_patterns():
    with open(os.path.join(ROOT, ".clang-format-ignore"), encoding="utf-8") as f:
        lines = (line.strip() for line in f)
        return [line for line in lines if line and not line.startswith("#")]


def own_files(extensions):
    tracked = subprocess.run(
        ["git", "ls-files", "Application"], cwd=ROOT, capture_output=True, text=True, check=True
    ).stdout.split()
    patterns = ignored_patterns()
    return [
        path
        for path in tracked
        if path.endswith(extensions) and not any(fnmatch.fnmatch(path, p) for p in patterns)
    ]


def toolchain_sysroot():
    with open(os.path.join(COMPILE_DB, "compile_commands.json"), encoding="utf-8") as f:
        compiler = json.load(f)[0]["command"].split()[0]
    sysroot = subprocess.run([compiler, "-print-sysroot"], capture_output=True, text=True, check=True)
    return os.path.normpath(sysroot.stdout.strip())


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else ""
    if command == "format":
        args = ["clang-format", "-i", *own_files((".c", ".h"))]
    elif command == "check":
        args = ["clang-format", "--dry-run", "--Werror", *own_files((".c", ".h"))]
    elif command == "tidy":
        if not os.path.exists(os.path.join(COMPILE_DB, "compile_commands.json")):
            sys.exit("build/Debug/compile_commands.json not found: run cmake --preset Debug first")
        args = ["clang-tidy", "-p", COMPILE_DB, f"--extra-arg=--sysroot={toolchain_sysroot()}", *own_files((".c",))]
    else:
        sys.exit(__doc__)
    sys.exit(subprocess.run(args, cwd=ROOT).returncode)


if __name__ == "__main__":
    main()
