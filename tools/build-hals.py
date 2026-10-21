#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Cross-compile HAL directories with the NDK against fetched AOSP headers, and check them.

    tools/build-hals.py --ndk <ndk> --api <level> --headers <dir> --out <dir> <hal-dir> ...

<ndk> is the NDK pinned in tools/ndk.env (tools/fetch-ndk.sh), <level> the API level of the
release whose headers are in <headers> (19 for 4.4, 23 for 6.0), and <headers> an output
directory of tools/fetch-aosp-headers.sh. Each HAL directory's
Android.mk is the build description: LOCAL_MODULE, LOCAL_SRC_FILES, LOCAL_CFLAGS and
LOCAL_SHARED_LIBRARIES are read from it (plain := and += assignments only). Each module is
built as an ARMv7-A shared library with -Wall -Werror, then checked:
  - it exports HAL_MODULE_INFO_SYM (HMI);
  - it needs only libc, libm, libdl and the libraries its Android.mk lists;
  - every undefined symbol is defined by the NDK's stub for an NDK library it links
    (libc, libm, libdl, liblog at API level <level>), or is listed in
    tools/hal-symbols.allow for a non-NDK system library it lists in LOCAL_SHARED_LIBRARIES.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALLOW_FILE = ROOT / "tools" / "hal-symbols.allow"
TRIPLE = "armv7a-linux-androideabi"
NDK_LIBS = ("libc", "libm", "libdl", "liblog")
IMPLICIT_LIBS = ("libc", "libm", "libdl")
# Header roots inside the fetched tree, used when present.
INCLUDE_DIRS = (
    "hardware/libhardware/include",
    "system/core/include",
    "system/media/audio/include",
    "external/tinyalsa/include",
)
CFLAGS = ["-std=gnu99", "-O2", "-fPIC", "-Wall", "-Werror", "-fno-common"]
# What the AOSP build itself defines for these headers. 4.4 force-includes
# build/core/combo/include/arch/linux-arm/AndroidConfig.h, which sets HAVE_SYS_UIO_H (line 114
# at android-4.4.4_r2); without it, 4.4's log/uio.h redefines struct iovec. 6.0 ignores it.
PLATFORM_DEFINES = ["-DHAVE_SYS_UIO_H=1"]
VARS = ("LOCAL_MODULE", "LOCAL_SRC_FILES", "LOCAL_CFLAGS", "LOCAL_SHARED_LIBRARIES")
ASSIGN_RE = re.compile(r"^(\w+)\s*(:=|\+=|=)\s*(.*)$")


def parse_android_mk(text):
    """Values of VARS in a single-module Android.mk, as lists of words."""
    out = {name: [] for name in VARS}
    logical = []
    for line in text.replace("\\\n", " ").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            logical.append(line)
    modules = 0
    for line in logical:
        if line.startswith("include $(CLEAR_VARS)"):
            modules += 1
        m = ASSIGN_RE.match(line)
        if not m or m.group(1) not in VARS:
            continue
        name, op, value = m.groups()
        if "$(" in value:
            raise ValueError(f"{name}: make functions are not supported: {value}")
        if op == "+=":
            out[name] += value.split()
        else:
            out[name] = value.split()
    if modules != 1:
        raise ValueError(f"expected one module, found {modules}")
    if len(out["LOCAL_MODULE"]) != 1 or not out["LOCAL_SRC_FILES"]:
        raise ValueError("LOCAL_MODULE and LOCAL_SRC_FILES are required")
    return out


def read_allowlist(text):
    """{library: {symbol, ...}} from lines of '<library> <symbol>'."""
    allow = {}
    for n, line in enumerate(text.splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 2:
            raise ValueError(f"{ALLOW_FILE.name}:{n}: expected '<library> <symbol>'")
        allow.setdefault(parts[0], set()).add(parts[1])
    return allow


def undefined_problems(undefined, ndk_symbols, allow, libs):
    """Undefined symbols not provided by the linked NDK stubs or allowed for listed libs."""
    ok = set()
    for lib in set(libs) | set(IMPLICIT_LIBS):
        ok |= ndk_symbols.get(lib, set())
        ok |= allow.get(lib, set())
    return sorted(s for s in undefined if s not in ok)


def nm(tool, path, defined):
    flag = "--defined-only" if defined else "--undefined-only"
    out = subprocess.run([tool, "-D", flag, "--format=posix", str(path)],
                         check=True, capture_output=True, text=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2:
            syms[parts[0].split("@")[0]] = parts[1]
    return syms


def needed(tool, path):
    out = subprocess.run([tool, "-d", str(path)], check=True, capture_output=True,
                         text=True).stdout
    return re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", out)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ndk", default=os.environ.get("ANDROID_NDK_HOME"))
    ap.add_argument("--api", type=int, required=True)
    ap.add_argument("--headers", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("hals", nargs="+")
    args = ap.parse_args(argv)
    if not args.ndk:
        ap.error("no NDK: pass --ndk or set ANDROID_NDK_HOME")

    bindirs = list(Path(args.ndk).glob("toolchains/llvm/prebuilt/*/bin"))
    if len(bindirs) != 1:
        sys.exit(f"error: no single NDK LLVM toolchain under {args.ndk}")
    bindir = bindirs[0]
    cc = bindir / f"{TRIPLE}{args.api}-clang"
    if not cc.exists():
        sys.exit(f"error: this NDK has no compiler for API level {args.api}: {cc}")
    llvm_nm = bindir / "llvm-nm"
    readelf = bindir / "llvm-readelf"
    stubs = bindir.parent / "sysroot" / "usr" / "lib" / "arm-linux-androideabi" / str(args.api)
    ndk_symbols = {lib: set(nm(llvm_nm, stubs / f"{lib}.so", True)) for lib in NDK_LIBS}
    allow = read_allowlist(ALLOW_FILE.read_text()) if ALLOW_FILE.exists() else {}

    headers = Path(args.headers)
    includes = []
    for rel in INCLUDE_DIRS:
        if (headers / rel).is_dir():
            includes += ["-isystem", str(headers / rel)]
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    failed = False
    for hal in args.hals:
        src = ROOT / hal
        mk = parse_android_mk((src / "Android.mk").read_text())
        module = mk["LOCAL_MODULE"][0]
        libs = mk["LOCAL_SHARED_LIBRARIES"]
        so = out / f"{module}.so"
        link = [f"-l{lib[3:]}" for lib in libs if lib in NDK_LIBS]
        cmd = ([str(cc)] + CFLAGS + PLATFORM_DEFINES + mk["LOCAL_CFLAGS"] + includes + ["-I", str(src)]
               + [str(src / f) for f in mk["LOCAL_SRC_FILES"]]
               + ["-shared", f"-Wl,-soname,{module}.so", "-o", str(so)] + link)
        print(f"== {hal}: {module}", flush=True)
        r = subprocess.run(cmd)
        if r.returncode:
            print(f"FAIL {module}: compile", flush=True)
            failed = True
            continue
        problems = []
        exported = nm(llvm_nm, so, True)
        if exported.get("HMI") not in ("D", "R", "B"):
            problems.append("HAL_MODULE_INFO_SYM (HMI) is not an exported data symbol")
        allowed_needed = {f"{lib}.so" for lib in set(libs) | set(IMPLICIT_LIBS)}
        for lib in needed(readelf, so):
            if lib not in allowed_needed:
                problems.append(f"needs {lib}, not in LOCAL_SHARED_LIBRARIES")
        for sym in undefined_problems(nm(llvm_nm, so, False), ndk_symbols, allow, libs):
            problems.append(f"undefined symbol {sym} is not provided by its libraries")
        for p in problems:
            print(f"FAIL {module}: {p}", flush=True)
        if problems:
            failed = True
        else:
            print(f"ok {module}: {so.stat().st_size} bytes, needs {needed(readelf, so)}",
                  flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
