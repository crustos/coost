#!/usr/bin/env python3
"""Build coost through the Crust C++ subset.

The C++ part of the library is lowered as ONE translation unit: every
source in SOURCES is spliced into build/co_all.cc, lowered by crust's
cpprust.py to build/co.c, then compiled by a C compiler. That is how Crust
consumes C++ (one unit, no link boundary), and it means one cpprust run per
build.

C_SOURCES are plain C (sockets, the HTTP protocol). They skip cpprust
entirely, are compiled once into build/c/*.o and only rebuilt when they or
include/co/c/*.h change, and are linked into everything. Lowering is the
slow step, so code that needs no C++ features belongs there.

    python3 build.py            # build/co.c, build/co.o and build/libco.a
    python3 build.py test       # also lower, compile and run tests/*.cc
    python3 build.py cxx        # cross-check: compile the same sources with g++
    python3 build.py clean

Tests are lowered as their own units, each holding only the library sources
it reaches: src/X.cc joins when include/co/X.h is in the test's include
closure. A lowering is skipped when nothing it splices has changed (the key
hashes the closure, cpprust.py and the flags), and independent lowerings run
in parallel (JOBS, default: the CPU count).

Environment: CRUST (path to the crust checkout, default ../crust),
CC (default cc), CXX (default g++), ASAN=1 (tests under ASan/UBSan),
JOBS (parallel lowerings).
"""
import concurrent.futures
import hashlib
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(ROOT, "build")
INCLUDE = os.path.join(ROOT, "include")
CRUST = os.environ.get("CRUST", os.path.join(ROOT, "..", "crust"))
CPPRUST = os.path.join(CRUST, "tools", "cpprust.py")
CC = os.environ.get("CC", "cc")
CXX = os.environ.get("CXX", "g++")

# Order matters only for readability of the generated C.
SOURCES = [
    "src/mem.cc",
    "src/fast.cc",
    "src/fastring.cc",
    "src/str.cc",
    "src/path.cc",
    "src/time.cc",
    "src/fs.cc",
    "src/json.cc",
    "src/hash/murmur_hash.cc",
    "src/hash/crc16.cc",
    "src/hash/md5.cc",
    "src/hash/sha256.cc",
    "src/hash/base64.cc",
    "src/hash/url.cc",
    "src/http.cc",
]

# Plain C, compiled directly. Headers in include/co/c/ wrap their
# declarations in extern "C" so the g++ cross-check links against them too.
C_SOURCES = [
    "src/c/sock.c",
    "src/c/http.c",
    "src/c/http_server.c",
]

CFLAGS = ["-std=gnu11", "-O2", "-Wall", "-Wno-unused-function"]
if os.environ.get("ASAN"):
    CFLAGS += ["-g", "-fsanitize=address,undefined"]
CXXFLAGS = ["-std=c++11", "-O2", "-Wall", "-D_GNU_SOURCE"]


JOBS = int(os.environ.get("JOBS", "0")) or os.cpu_count() or 1
LOWER_FLAGS = ["--incdir", INCLUDE, "-D", "CO_CRUST", "--no-clang"]

# Lowered C does not depend on CFLAGS and is shared; anything compiled does,
# so it goes to a directory per flag set.
OUT = os.path.join(BUILD, "asan") if os.environ.get("ASAN") else BUILD


def show(cmd):
    print("  " + " ".join(os.path.relpath(c, ROOT) if c.startswith(ROOT) else c
                          for c in cmd), flush=True)


def run(cmd):
    show(cmd)
    r = subprocess.run(cmd)
    if r.returncode != 0:
        sys.exit(r.returncode)


# ---- what a unit is made of ------------------------------------------------

INC_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^">]+)[">]', re.M)


def closure(paths):
    """Every file reachable from @paths through #include, resolved the way
    cpprust resolves them: a quoted include from the including file's
    directory first, then include/; anything else is a system header and
    is not followed. Conditionals are ignored, which only over-approximates."""
    seen = set()
    stack = [os.path.normpath(p) for p in paths]
    while stack:
        p = stack.pop()
        if p in seen:
            continue
        seen.add(p)
        with open(p, errors="replace") as f:
            text = f.read()
        for kind, name in INC_RE.findall(text):
            cands = [os.path.join(os.path.dirname(p), name)] if kind == '"' else []
            cands.append(os.path.join(INCLUDE, name))
            for c in cands:
                if os.path.isfile(c):
                    stack.append(os.path.normpath(c))
                    break
    return seen


def header_of(src):
    """include/co/X.h for src/X.cc, or None"""
    h = os.path.join(INCLUDE, "co", src[len("src/"):-len(".cc")] + ".h")
    return h if os.path.isfile(h) else None


def select_sources(extra):
    """The library sources a unit built from @extra needs, and the unit's
    include closure."""
    files = closure([os.path.join(ROOT, e) for e in extra])
    chosen = set()
    changed = True
    while changed:
        changed = False
        for s in SOURCES:
            if s in chosen:
                continue
            h = header_of(s)
            if h is None or h in files:  # no header to go by: always take it
                chosen.add(s)
                files |= closure([os.path.join(ROOT, s)])
                changed = True
    return [s for s in SOURCES if s in chosen], files


def unit_key(files):
    h = hashlib.sha256()
    for part in [CPPRUST] + sorted(files):
        h.update(part.encode())
        with open(part, "rb") as f:
            h.update(f.read())
    h.update(" ".join(LOWER_FLAGS).encode())
    return h.hexdigest()


def unity(path, sources):
    """Write a .cc that includes @sources, only when it would change."""
    text = "/* generated by build.py -- do not edit */\n" + "".join(
        '#include "%s"\n' % os.path.relpath(os.path.join(ROOT, s), BUILD) for s in sources)
    if not os.path.exists(path) or open(path).read() != text:
        with open(path, "w") as f:
            f.write(text)


# ---- lowering ----------------------------------------------------------------

def lower_cmd(src, out):
    return [sys.executable, CPPRUST, src, "-o", out] + LOWER_FLAGS


def lower_job(src, out, key):
    """Lower @src to @out unless its key says it is current. Returns
    (skipped, cmd, returncode, output)."""
    stamp = out + ".key"
    if os.path.exists(out) and os.path.exists(stamp) and open(stamp).read() == key:
        return True, None, 0, ""
    cmd = lower_cmd(src, out)
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode == 0:
        # headers are spliced into one file, so their include guards are noise
        with open(out) as f:
            text = f.read()
        with open(out, "w") as f:
            f.write(text.replace("#pragma once\n", ""))
        with open(stamp, "w") as f:
            f.write(key)
    elif os.path.exists(stamp):
        os.remove(stamp)
    return False, cmd, r.returncode, r.stdout


def lower_all(jobs):
    """Run [(src, out, key)] in parallel; stop on the first failure. Returns
    the outputs that were actually re-lowered."""
    if not os.path.exists(CPPRUST):
        sys.exit("cpprust.py not found at %s (set CRUST=/path/to/crust)" % CPPRUST)
    fresh = set()
    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as ex:
        results = list(ex.map(lambda j: lower_job(*j), jobs))
    for (src, out, _), (skipped, cmd, rc, output) in zip(jobs, results):
        if skipped:
            print("  (current) " + os.path.relpath(out, ROOT))
            continue
        show(cmd)
        if output:
            sys.stdout.write(output)
        if rc != 0:
            sys.exit(rc)
        fresh.add(out)
    return fresh


def lower(src, out):
    """Lower one file unconditionally (kept for scripts that call it)."""
    lower_all([(src, out, "")])


def c_objects():
    """Compile C_SOURCES into build/c/, skipping objects that are current."""
    # objects built with different flags must not be mixed up
    out = os.path.join(OUT, "c")
    os.makedirs(out, exist_ok=True)
    hdir = os.path.join(INCLUDE, "co", "c")
    headers = [os.path.join(hdir, h) for h in os.listdir(hdir)] if os.path.isdir(hdir) else []
    newest_h = max([os.path.getmtime(h) for h in headers] + [0])
    objs = []
    for s in C_SOURCES:
        src = os.path.join(ROOT, s)
        obj = os.path.join(out, os.path.basename(s)[:-2] + ".o")
        if (not os.path.exists(obj) or
                os.path.getmtime(obj) < max(os.path.getmtime(src), newest_h)):
            run([CC] + CFLAGS + ["-I", INCLUDE, "-c", src, "-o", obj])
        objs.append(obj)
    return objs


def newer(target, deps):
    return not os.path.exists(target) or any(
        os.path.getmtime(d) > os.path.getmtime(target) for d in deps)


def build():
    os.makedirs(BUILD, exist_ok=True)
    objs = c_objects()
    src = os.path.join(BUILD, "co_all.cc")
    co_c = os.path.join(BUILD, "co.c")
    unity(src, SOURCES)
    files = closure([os.path.join(ROOT, s) for s in SOURCES])
    lower_all([(src, co_c, unit_key(files))])
    os.makedirs(OUT, exist_ok=True)
    co_o = os.path.join(OUT, "co.o")
    if newer(co_o, [co_c]):
        run([CC] + CFLAGS + ["-c", co_c, "-o", co_o])
    lib = os.path.join(OUT, "libco.a")
    if newer(lib, [co_o] + objs):
        if os.path.exists(lib):
            os.remove(lib)
        run(["ar", "rcs", lib, co_o] + objs)


def test_names():
    d = os.path.join(ROOT, "tests")
    return sorted(n[:-3] for n in os.listdir(d) if n.endswith(".cc")) if os.path.isdir(d) else []


def test_units():
    """(name, unity source) per test, each with only the sources it needs"""
    units = []
    for base in test_names():
        extra = "tests/%s.cc" % base
        sources, files = select_sources([extra])
        src = os.path.join(BUILD, base + "_all.cc")
        unity(src, sources + [extra])
        units.append((base, src, files))
    return units


def tests():
    objs = c_objects()
    units = test_units()
    jobs = [(src, os.path.join(BUILD, base + ".c"), unit_key(files))
            for base, src, files in units]
    lower_all(jobs)
    failed = []
    for base, _, _ in units:
        c = os.path.join(BUILD, base + ".c")
        exe = os.path.join(OUT, base)
        if newer(exe, [c] + objs):
            run([CC] + CFLAGS + [c] + objs + ["-o", exe, "-lm"])
        print("  ./" + os.path.relpath(exe, ROOT), flush=True)
        if subprocess.run([exe]).returncode != 0:
            failed.append(base)
    if failed:
        sys.exit("FAILED: " + " ".join(failed))
    print("all %d test(s) passed" % len(units))


def cxx():
    """Compile the same units as C++ -- catches mistakes the lowering
    would carry through silently."""
    os.makedirs(BUILD, exist_ok=True)
    objs = c_objects()
    for base, src, _ in test_units():
        exe = os.path.join(OUT, base + "_cxx")
        run([CXX] + CXXFLAGS + ["-I", INCLUDE, src] + objs + ["-o", exe])
        print("  ./" + os.path.relpath(exe, ROOT), flush=True)
        if subprocess.run([exe]).returncode != 0:
            sys.exit("FAILED (c++): " + base)


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "build"
    if cmd == "clean":
        shutil.rmtree(BUILD, ignore_errors=True)
    elif cmd == "build":
        build()
    elif cmd == "test":
        build()
        tests()
    elif cmd == "cxx":
        cxx()
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
