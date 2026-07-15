#!/usr/bin/env python3
"""ino-sim — render an Arduino .ino sketch's TFT screen output to PNG + HTML.

Compiles the sketch on the host against thin Arduino/GFX shims, redirecting all
drawing into an in-memory framebuffer (the real Adafruit_GFX rendering code), then
runs it through a scenario script that feeds inputs and snapshots the screen.

Usage:
    python3 simulate.py <sketch.ino> [--scenario FILE] [--backend auto|adafruit|tft_espi]
                                     [--scale N] [--out DIR] [--keep]

Outputs (default in <tooldir>/out):
    <name>.png  per `snap` frame
    index.html  gallery of all frames
"""
import argparse
import glob
import os
import re
import subprocess
import sys

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(TOOL_DIR, "src")
THIRD_PARTY_GFX = os.path.join(TOOL_DIR, "third_party", "Adafruit_GFX")
U8G2_VENDOR = os.path.join(TOOL_DIR, "third_party", "U8g2")   # vendored real U8g2 (minus fonts)
U8G2_CLIB = os.path.join(U8G2_VENDOR, "clib")
U8G2_BACKEND = os.path.join(SRC, "u8g2_backend")             # host HAL + font subset


def log(msg):
    print(f"[ino-sim] {msg}", file=sys.stderr)


# --------------------------------------------------------------------------
# .ino -> .cpp: the Arduino build prepends <Arduino.h> and auto-generates
# function prototypes so functions can be called before their definition. We
# reproduce both so a verbatim sketch compiles.
# --------------------------------------------------------------------------
# Matches a function definition's opening line, e.g.
#   void climatePage() {
#   uint16_t gradient(float r) {
#   static void foo(int a, int b) {
FUNC_DEF_RE = re.compile(
    r'^[ \t]*'
    r'((?:(?:static|inline|virtual|extern)\s+)*'      # optional specifiers
    r'(?:unsigned\s+|signed\s+)?'                      # sign
    r'[A-Za-z_]\w*(?:\s*<[^>]*>)?'                     # return type (+ template)
    r'(?:\s*[\*&]+|\s+))'                              # pointer/ref or space
    r'([A-Za-z_]\w*)\s*'                               # function name
    r'\(([^;{)]*)\)\s*'                                # arg list (no ; inside)
    r'\{',                                             # opening brace
    re.MULTILINE,
)

# Keywords that look like "type name(...) {" but are not functions to prototype.
NON_FUNC_NAMES = {"if", "for", "while", "switch", "do", "else", "return",
                  "setup", "loop"}


def strip_comments_and_strings(code):
    """Blank out comments and string/char literals so the prototype regex does
    not trip over braces/parens inside them. Preserves newlines/offsets."""
    out = []
    i, n = 0, len(code)
    while i < n:
        c = code[i]
        two = code[i:i+2]
        if two == "//":
            j = code.find("\n", i)
            j = n if j == -1 else j
            out.append(" " * (j - i))
            i = j
        elif two == "/*":
            j = code.find("*/", i + 2)
            j = n if j == -1 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in code[i:j]))
            i = j
        elif c == '"' or c == "'":
            q = c
            j = i + 1
            while j < n and code[j] != q:
                if code[j] == "\\":
                    j += 1
                j += 1
            j = min(j + 1, n)
            out.append(" " * (j - i))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def generate_prototypes(sketch_code):
    """Return a list of prototype declarations for top-level function defs."""
    scrubbed = strip_comments_and_strings(sketch_code)
    protos = []
    seen = set()
    for m in FUNC_DEF_RE.finditer(scrubbed):
        # Only treat as top-level if the brace before it is balanced, i.e. the
        # match starts at column reachable from file scope. Heuristic: the match
        # must not be indented inside another function — we approximate by
        # checking the brace depth up to this point is zero.
        start = m.start()
        depth = scrubbed.count("{", 0, start) - scrubbed.count("}", 0, start)
        if depth != 0:
            continue
        ret_and_space, name, args = m.group(1), m.group(2), m.group(3)
        if name in NON_FUNC_NAMES:
            continue
        sig = f"{ret_and_space.strip()} {name}({args.strip()});"
        if sig not in seen:
            seen.add(sig)
            protos.append(sig)
    return protos


# Matches a top-level struct/class definition opener, e.g.
#   struct Note {
#   class Pulser {
#   class Foo : public Bar {
TYPE_DEF_RE = re.compile(
    r'^[ \t]*(struct|class)\s+([A-Za-z_]\w*)\s*(?:final\b\s*)?(?::[^{;]*)?\{',
    re.MULTILINE,
)


def find_forward_decls(sketch_code):
    """Forward-declare the sketch's own struct/class types. Function prototypes
    are inserted right after the #include block — BEFORE these types' own
    definitions — so a prototype like `Note* noteByOffset(int)` would otherwise
    fail with "'Note' does not name a type". A leading `struct Note;` fixes any
    signature that uses the type by pointer/reference (the common case)."""
    scrubbed = strip_comments_and_strings(sketch_code)
    decls, seen = [], set()
    for m in TYPE_DEF_RE.finditer(scrubbed):
        start = m.start()
        if scrubbed.count("{", 0, start) - scrubbed.count("}", 0, start) != 0:
            continue  # nested type, not top-level
        kw, name = m.group(1), m.group(2)
        if name in seen:
            continue
        seen.add(name)
        decls.append("%s %s;" % (kw, name))
    return decls


def detect_backend(sketch_code):
    s = sketch_code
    if re.search(r'#\s*include\s*[<"]U8g2lib\.h[>"]', s):
        return "u8g2"
    if re.search(r'#\s*include\s*[<"]TFT_eSPI\.h[>"]', s):
        return "tft_espi"
    if re.search(r'#\s*include\s*[<"]Adafruit_ST77', s):
        return "adafruit"
    if re.search(r'#\s*include\s*[<"]Adafruit_GFX\.h[>"]', s):
        return "adafruit"
    return "adafruit"  # sensible default


def find_proto_insert_line(code):
    """Mirror the Arduino preprocessor: insert prototypes AFTER the sketch's
    leading directives (its #include / #define / #if block), so prototypes that
    reference library types (GFXfont, String, ...) compile. Returns the line
    index (0-based) at which to insert, skipping comments, blank lines, and
    preprocessor directives — including multi-line directives (trailing '\\')."""
    lines = code.splitlines()
    last = 0  # default: top of file
    i = 0
    in_block_comment = False
    while i < len(lines):
        raw = lines[i]
        s = raw.strip()
        if in_block_comment:
            if "*/" in s:
                in_block_comment = False
                after = s.split("*/", 1)[1].strip()
                if after == "":
                    i += 1
                    continue
            else:
                i += 1
                continue
        if s == "" or s.startswith("//"):
            i += 1
            continue
        if s.startswith("/*"):
            if "*/" not in s:
                in_block_comment = True
            i += 1
            continue
        if s.startswith("#"):
            # consume line continuations
            while raw.rstrip().endswith("\\") and i + 1 < len(lines):
                i += 1
                raw = lines[i]
            last = i + 1
            i += 1
            continue
        # First real code line — stop; prototypes go just before it.
        break
    return last


def build_cpp(sketch_path, build_dir):
    # Arduino sketches are UTF-8; force it so the locale default (e.g. GBK on a
    # Chinese Windows) doesn't corrupt the read or fail the write.
    with open(sketch_path, "r", encoding="utf-8", errors="replace") as f:
        code = f.read()
    protos = generate_prototypes(code)
    fwds = find_forward_decls(code)
    backend = detect_backend(code)

    lines = code.splitlines()
    insert_at = find_proto_insert_line(code)

    proto_block = []
    proto_block.append("")
    proto_block.append("// --- auto-generated forward decls + prototypes (Arduino preprocessor) ---")
    proto_block.extend(fwds)
    proto_block.extend(protos)
    proto_block.append("// --- end prototypes ---")
    # Restore correct line numbers for compiler diagnostics into the .ino.
    proto_block.append('#line %d "%s"' % (insert_at + 1, sketch_path.replace("\\", "/")))

    body = (lines[:insert_at] + proto_block + lines[insert_at:])
    # <Arduino.h> first so the core API exists even before the sketch's includes.
    cpp = ('#include <Arduino.h>\n'
           + '#line 1 "%s"\n' % sketch_path.replace("\\", "/")
           + "\n".join(body) + "\n")

    os.makedirs(build_dir, exist_ok=True)
    cpp_path = os.path.join(build_dir, "sketch.cpp")
    with open(cpp_path, "w", encoding="utf-8") as f:
        f.write(cpp)
    return cpp_path, backend, protos


def compile_and_link_u8g2(cpp_path, build_dir, exe_path):
    """U8g2 sketches (monochrome OLED). Compiles the vendored *real* U8g2 C core
    (cached) plus a host HAL that captures its full framebuffer, and links that
    with the runtime + sketch. Pixel-faithful, including the real CJK/Latin
    fonts. C files use the C compiler; C++ (sketch/runtime/wrapper) uses CXX."""
    cc = os.environ.get("CC", "gcc" if os.name == "nt" else "cc")
    cxx = os.environ.get("CXX", "g++" if os.name == "nt" else "clang++")
    obj_dir = os.path.join(build_dir, "u8g2_obj")
    os.makedirs(obj_dir, exist_ok=True)

    c_inc = ["-I", U8G2_CLIB, "-I", U8G2_VENDOR]

    # 1) Vendored U8g2 C core + font subset → cached .o. The library never
    #    changes, so each file compiles once; delete build/u8g2_obj to rebuild.
    c_sources = sorted(glob.glob(os.path.join(U8G2_CLIB, "*.c")))
    c_sources.append(os.path.join(U8G2_BACKEND, "u8g2_fonts_subset.c"))
    if len(c_sources) < 2:
        log("u8g2: vendored clib missing under %s" % U8G2_CLIB)
        return False
    objs = []
    to_build = [c for c in c_sources
                if not os.path.isfile(os.path.join(obj_dir,
                    os.path.splitext(os.path.basename(c))[0] + ".o"))]
    if to_build:
        log("u8g2: compiling C core (%d/%d files; cached afterwards)..."
            % (len(to_build), len(c_sources)))
    for c in c_sources:
        o = os.path.join(obj_dir, os.path.splitext(os.path.basename(c))[0] + ".o")
        if not os.path.isfile(o):
            proc = subprocess.run([cc, "-O1", "-w", "-c"] + c_inc + [c, "-o", o],
                                  capture_output=True, text=True, errors="replace")
            if proc.returncode != 0:
                log("u8g2 C compile FAILED: %s" % os.path.basename(c))
                sys.stderr.write(proc.stdout); sys.stderr.write(proc.stderr)
                return False
        objs.append(o)

    # 2) C++ side: sketch + runtime + vendored U8g2 C++ wrapper + host HAL.
    cxx_sources = [
        cpp_path,
        os.path.join(SRC, "arduino_shim", "arduino.cpp"),
        os.path.join(SRC, "runtime", "main.cpp"),
        os.path.join(SRC, "runtime", "scenario.cpp"),
        os.path.join(SRC, "runtime", "png_writer.cpp"),
        os.path.join(SRC, "runtime", "html_viewer.cpp"),
        os.path.join(U8G2_VENDOR, "U8g2lib.cpp"),
        os.path.join(U8G2_BACKEND, "u8g2_sim_hal.cpp"),
    ]
    cxx_inc = ["-I", os.path.join(SRC, "arduino_shim"),
               "-I", os.path.join(SRC, "runtime"),
               "-I", U8G2_BACKEND, "-I", U8G2_VENDOR, "-I", U8G2_CLIB]
    # SIM_DEMO strips the sketch's networking; INO_SIM enables scenario injection;
    # SIM_U8G2 turns on the runtime's monochrome-buffer snapshot path.
    cmd = [cxx, "-std=c++17", "-O1", "-w",
           "-DARDUINO=10819", "-DESP32=1", "-DINO_SIM=1", "-DSIM_DEMO=1", "-DSIM_U8G2=1",
           "-Wno-narrowing", "-fno-strict-aliasing"]
    cmd += cxx_inc + cxx_sources + objs + ["-o", exe_path]

    log("u8g2: compiling sketch + runtime and linking...")
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    if proc.returncode != 0:
        log("u8g2 link FAILED:")
        sys.stderr.write(proc.stdout); sys.stderr.write(proc.stderr)
        return False
    if proc.stderr.strip():
        sys.stderr.write(proc.stderr)
    return True


def compile_and_link(cpp_path, backend, build_dir, exe_path):
    if backend == "u8g2":
        return compile_and_link_u8g2(cpp_path, build_dir, exe_path)
    include_dirs = [
        os.path.join(SRC, "arduino_shim"),
        os.path.join(SRC, "gfx_backend"),
        os.path.join(SRC, "tft_espi_backend"),
        os.path.join(SRC, "runtime"),
        THIRD_PARTY_GFX,
    ]
    sources = [
        cpp_path,
        os.path.join(SRC, "arduino_shim", "arduino.cpp"),
        os.path.join(THIRD_PARTY_GFX, "Adafruit_GFX.cpp"),
        os.path.join(SRC, "runtime", "main.cpp"),
        os.path.join(SRC, "runtime", "scenario.cpp"),
        os.path.join(SRC, "runtime", "png_writer.cpp"),
        os.path.join(SRC, "runtime", "html_viewer.cpp"),
    ]
    cxx = os.environ.get("CXX", "g++" if os.name == "nt" else "clang++")
    # -DARDUINO/-DESP32 must be on the command line (not just in Arduino.h) so
    # third_party Adafruit_GFX.cpp — which includes Adafruit_GFX.h before any of
    # our headers — takes the ARDUINO>=100 / ESP32 code paths.
    cmd = [cxx, "-std=c++17", "-O1", "-w",
           "-DARDUINO=10819", "-DESP32=1", "-DINO_SIM=1",
           "-Wno-narrowing", "-fno-strict-aliasing"]
    for d in include_dirs:
        cmd += ["-I", d]
    cmd += sources
    # png_writer is self-contained (no zlib), so nothing to link beyond libc++.
    cmd += ["-o", exe_path]

    log("compiling (%s backend)..." % backend)
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    if proc.returncode != 0:
        log("compile FAILED:")
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        return False
    if proc.stderr.strip():
        sys.stderr.write(proc.stderr)
    return True


def main():
    ap = argparse.ArgumentParser(description="Render an Arduino .ino TFT sketch to images.")
    ap.add_argument("sketch", help="path to the .ino sketch")
    ap.add_argument("--scenario", default=None, help="scenario file (default: scenarios/default.scn)")
    ap.add_argument("--backend", default="auto", choices=["auto", "adafruit", "tft_espi", "u8g2"])
    ap.add_argument("--scale", type=int, default=4, help="integer upscale for PNGs (default 4)")
    ap.add_argument("--out", default=None, help="output dir (default: <tool>/out)")
    ap.add_argument("--keep", action="store_true", help="keep the generated sketch.cpp")
    args = ap.parse_args()

    sketch_path = os.path.abspath(args.sketch)
    if not os.path.isfile(sketch_path):
        log("no such sketch: %s" % sketch_path)
        return 2

    scenario = args.scenario or os.path.join(TOOL_DIR, "scenarios", "default.scn")
    scenario = os.path.abspath(scenario)
    if not os.path.isfile(scenario):
        log("no such scenario: %s" % scenario)
        return 2

    build_dir = os.path.join(TOOL_DIR, "build")
    out_dir = os.path.abspath(args.out) if args.out else os.path.join(TOOL_DIR, "out")
    os.makedirs(out_dir, exist_ok=True)

    cpp_path, backend, protos = build_cpp(sketch_path, build_dir)
    if args.backend != "auto":
        backend = args.backend
    log("backend=%s, %d prototype(s) generated" % (backend, len(protos)))

    # Windows compilers append .exe; keep the launch path in sync with what the
    # compiler actually writes so subprocess.run below can find the binary.
    exe_path = os.path.join(build_dir, "sim" + (".exe" if os.name == "nt" else ""))
    if not compile_and_link(cpp_path, backend, build_dir, exe_path):
        return 1

    log("running scenario: %s" % os.path.relpath(scenario, TOOL_DIR))
    run = subprocess.run([exe_path, scenario, "--out", out_dir,
                          "--scale", str(args.scale)],
                         capture_output=True, text=True, errors="replace")
    sys.stderr.write(run.stderr)
    if run.returncode != 0:
        log("run FAILED (exit %d)" % run.returncode)
        return run.returncode

    # Surface the artifact paths.
    print(run.stdout, end="")
    html = os.path.join(out_dir, "index.html")
    log("done. open: %s" % html)

    if not args.keep:
        try:
            os.remove(cpp_path)
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
