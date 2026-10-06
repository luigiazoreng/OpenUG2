#!/usr/bin/env python3
"""Reproduce the Vita source API inventory, without assets or a C toolchain.

This is a lexical inventory, not a preprocessor or a reachability analysis.
It includes conditional diagnostic paths in engine translation units, removes
comments/literals, and lists GL entry points loaded by name separately.
"""
import argparse
from collections import defaultdict
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = "addbcbe2a8d3ec6911004fad418443f96c9ebe1f"
LIBC = set("""
fopen fclose fread fwrite fseek ftell fflush printf fprintf sprintf snprintf
puts perror sscanf atoi atof strtol strtoul strtof strtod getenv exit abort
malloc calloc realloc free memcpy memmove memset memcmp strlen strcmp strncmp
strcpy strncpy strcat strncat strchr strrchr strstr strtok qsort bsearch
tolower toupper isspace isdigit isalpha isalnum assert rand srand abs memchr strspn putchar
open close fstat mmap munmap opendir readdir closedir scandir alphasort clock clock_gettime
compress2 compressBound crc32
fabs fabsf sqrt sqrtf pow powf exp expf sin sinf cos cosf tan tanf atan atan2
atan2f atanf logf asin asinf acos acosf floor floorf ceil ceilf round roundf fmod fmodf
fmin fminf fmax fmaxf hypot hypotf copysign copysignf isfinite isnan
""".split())
TOKEN = re.compile(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
FUNCTION = re.compile(r'\b([A-Za-z_]\w*)\s*\([^();{}]*(?:\([^()]*\)[^();{}]*)*\)\s*\{')
CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\(')


def clean(text):
    masked = TOKEN.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), text)
    return re.sub(r"(?m)^[ \t]*#(?:[^\n]*\\\n)*[^\n]*",
                  lambda m: re.sub(r"[^\n]", " ", m.group()), masked)


def functions(code):
    spans = []
    for m in FUNCTION.finditer(code):
        if m[1] in {"if", "for", "while", "switch"}:
            continue
        beg = m.end() - 1
        depth = 1
        end = beg + 1
        while end < len(code) and depth:
            depth += (code[end] == "{") - (code[end] == "}")
            end += 1
        spans.append((beg, end, m[1]))
    return spans


def support(api):
    if api in {"mmap", "munmap"}:
        return "No file-backed mmap in Vita newlib", "read-only heap/seekable resource backend", "yes, engine"
    if api == "glPolygonMode":
        return "desktop diagnostic; excluded by N2_GLES", "keep excluded", "no"
    if api == "glGetUniformfv":
        return "absent in audited vitaGL header and archive", "track renderer-owned uniform state", "yes, engine renderer"
    if api in {"glCreateShader", "glShaderSource", "glCompileShader", "glLinkProgram"}:
        return "vitaGL GLSL translator + runtime compiler", "validate source and compiler module on device", "gate, V1"
    if api.startswith("gl"):
        return "vitaGL API; semantics/device test pending", "capability checks; see VITA_PORT", "test gate"
    if api == "SDL_SetRelativeMouseMode":
        return "desktop mouse interaction", "use stick camera input", "yes, interaction"
    if api.startswith("SDL_GL_") or api == "SDL_CreateWindow":
        return "SDL2 vitaGL backend required", "sdl2_vitagl; one context owner", "gate, V0"
    if api in {"SDL_GetKeyboardState"}:
        return "API exists; no physical keyboard", "map gamepad state to engine input", "yes, drive"
    if api.startswith("SDL_"):
        return "SDL2 Vita implementation", "keep; verify device behavior", "no API blocker"
    if api == "clock_gettime":
        return "newlib Vita CLOCK_MONOTONIC implemented", "keep; check return values", "no"
    if api == "getenv":
        return "newlib; shell environment not a launch UI", "use explicit diagnostics config later", "no"
    if api in {"opendir", "readdir", "closedir", "scandir", "alphasort", "open", "close", "fstat"}:
        return "Vita newlib I/O", "preserve case/root; measure storage", "no API blocker"
    if api in {"malloc", "calloc", "realloc", "free"}:
        return "newlib heap", "measure simultaneous residents/decoded bytes", "memory gate"
    if api in {"compress2", "compressBound", "crc32"}:
        return "Vita zlib", "link z; screenshots optional", "no"
    return "C99/newlib/libm", "keep; audit sizes/allocation and I/O errors", "no API blocker"


def run(revision):
    paths = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", revision, "src"], cwd=ROOT, text=True).splitlines()
    tables = {"engine": defaultdict(lambda: defaultdict(set)), "optional": defaultdict(lambda: defaultdict(set))}
    indirect = set()
    for path in paths:
        if Path(path).suffix not in {".c", ".h", ".cpp"} or "generated_" in path:
            continue
        raw = subprocess.check_output(["git", "show", f"{revision}:{path}"], cwd=ROOT, text=True)
        code = clean(raw)
        spans = functions(code)
        optional = path.startswith("src/frontend/") or path == "src/debugui.cpp"
        table = tables["optional" if optional else "engine"]
        # alphasort is passed to scandir as a callback, rather than called
        # syntactically. Keep that external libc dependency in the matrix.
        matches = list(CALL.finditer(code)) + list(re.finditer(r'\b(alphasort)\b', code))
        for m in matches:
            api = m[1]
            if not (api.startswith("SDL_") or re.match(r"gl[A-Z]", api) or api in LIBC):
                continue
            # Prototypes/definitions are not calls. Calls outside a function
            # body (e.g. macro wrappers) are retained with an explicit label.
            # External GL/SDL prototypes do not live in this source tree.
            owners = [(e - b, n) for b, e, n in spans if b <= m.start() < e]
            owner = min(owners)[1] if owners else "file/macro"
            if owner == "file/macro" and api in {"assert", "isfinite"}:
                continue
            line = code.count("\n", 0, m.start()) + 1
            table[api][path].add((owner, line))
        for m in re.finditer(r'SDL_GL_GetProcAddress\s*\(\s*"(gl\w+)"', raw):
            indirect.add((m[1], path, raw.count("\n", 0, m.start()) + 1))
    out = ["# Vita API inventory", "", f"Audited revision: `{revision}`.", "",
           "Generated by `python3 tools/vita_api_audit.py`. Comments and string literals",
           "are excluded. This is a source-level superset: conditional diagnostics in",
           "engine files are included, even when a feature is disabled. It is not proof",
           "of runtime reachability or hardware compatibility. Line numbers refer to",
           "the audited revision. Third-party ImGui backends and host tools are excluded",
           "from production; the optional first-party menu/debug wrapper is separate.", ""]
    for scope, table in tables.items():
        out += [f"## {'Engine production translation units and shared headers' if scope == 'engine' else 'Optional menu and DEBUG_UI wrapper'}", ""]
        for label, pred in [("OpenGL", lambda a: bool(re.match(r"gl[A-Z]", a))),
                            ("SDL", lambda a: a.startswith("SDL_")),
                            ("libc, POSIX, math and zlib", lambda a: a in LIBC)]:
            apis = sorted(a for a in table if pred(a))
            out += [f"### {label}: {len(apis)} unique names", "",
                    "| Current API | File | Function (source lines) | Vita support | Replacement / work | Blocker? |",
                    "| --- | --- | --- | --- | --- | --- |"]
            for api in apis:
                compat, work, blocker = support(api)
                for path, uses in sorted(table[api].items()):
                    byfunc = defaultdict(list)
                    for fun, line in sorted(uses):
                        byfunc[fun].append(str(line))
                    locations = "; ".join(f"`{fun}` ({', '.join(lines)})" for fun, lines in sorted(byfunc.items()))
                    out.append(f"| `{api}` | `{path}` | {locations} | {compat} | {work} | {blocker} |")
            out.append("")
    out += ["## GL entry points loaded indirectly", "",
            "These are additional production names invoked through `pf_*` pointers.",
            "They are optional GPU profiling, gated on `GL_EXT_timer_query` and all",
            "required pointers. Unsupported timers must report unavailable, not zero.", "",
            "| Entry point | File | Lookup line | Work |", "| --- | --- | --- | --- |"]
    for api, path, line in sorted(indirect):
        out.append(f"| `{api}` | `{path}` | {line} | validate timer target/result semantics; keep CPU timing fallback |")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", default=BASE)
    parser.add_argument("--output", type=Path, default=ROOT / "docs/VITA_API_INVENTORY.md")
    args = parser.parse_args()
    args.output.write_text(run(args.revision))
    print(args.output)
