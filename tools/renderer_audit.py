#!/usr/bin/env python3
"""Generate a source-level inventory of direct GL calls at a pinned revision."""
import argparse
from collections import defaultdict
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REVISION = "3f7536eba3ecfe6640e0115e57b0a48c49db53aa"
COMMENT_OR_STRING = re.compile(
    r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
)
FUNCTION = re.compile(r'\b([A-Za-z_]\w*)\s*\([^();{}]*(?:\([^()]*\)[^();{}]*)*\)\s*\{')
CALL = re.compile(r'\b(gl[A-Z]\w*)\s*\(')
PROC_LOOKUP = re.compile(r'SDL_GL_GetProcAddress\s*\(\s*"(gl\w+)"')

CREATE = {"glGenBuffers", "glGenTextures", "glGenFramebuffers",
          "glGenRenderbuffers", "glGenQueries", "glCreateProgram", "glCreateShader"}
DELETE = {"glDeleteBuffers", "glDeleteTextures", "glDeleteFramebuffers",
          "glDeleteRenderbuffers", "glDeleteQueries", "glDeleteProgram", "glDeleteShader"}
UPLOAD = {"glBufferData", "glBufferSubData", "glTexImage2D", "glTexSubImage2D",
          "glCompressedTexImage2D", "glCompressedTexSubImage2D", "glCopyTexImage2D",
          "glCopyTexSubImage2D", "glTexStorage2D", "glRenderbufferStorage",
          "glTexParameterf", "glTexParameteri", "glPixelStorei"}
PROGRAM_BUILD = {"glAttachShader", "glBindAttribLocation", "glCompileShader",
                 "glLinkProgram", "glShaderSource"}
RESOURCE_SETUP = {"glFramebufferRenderbuffer", "glFramebufferTexture2D", "glGenerateMipmap"}
DRAW = {"glDrawArrays", "glDrawElements", "glDrawRangeElements", "glDrawArraysInstanced",
        "glDrawElementsInstanced"}
FRAME = {"glClear", "glClearColor", "glClearDepth", "glClearDepthf", "glClearStencil", "glFlush", "glFinish"}
READBACK = {"glReadPixels", "glReadBuffer", "glGetError", "glGetString",
            "glGetBooleanv", "glGetFloatv", "glGetIntegerv", "glGetProgramiv",
            "glGetShaderiv", "glGetShaderInfoLog", "glGetProgramInfoLog",
            "glGetUniformLocation", "glGetAttribLocation", "glGetUniformfv",
            "glCheckFramebufferStatus", "glGetQueryObjectuiv", "glGetQueryObjectui64v",
            "glGetVertexAttribiv", "glIsEnabled", "glIsTexture"}
INDIRECT = {"glGenQueries", "glBeginQuery", "glEndQuery", "glGetQueryObjectuiv",
            "glGetQueryObjectui64v"}

OBJECTS = [
    ("buffer", "glGenBuffers", "glDeleteBuffers"),
    ("texture", "glGenTextures", "glDeleteTextures"),
    ("framebuffer", "glGenFramebuffers", "glDeleteFramebuffers"),
    ("renderbuffer", "glGenRenderbuffers", "glDeleteRenderbuffers"),
    ("query", "glGenQueries", "glDeleteQueries"),
    ("program", "glCreateProgram", "glDeleteProgram"),
    ("shader", "glCreateShader", "glDeleteShader"),
]


def scrub(raw):
    return COMMENT_OR_STRING.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), raw)


def function_spans(code):
    spans = []
    for m in FUNCTION.finditer(code):
        if m[1] in {"if", "for", "while", "switch"}:
            continue
        begin = m.end() - 1
        depth, end = 1, begin + 1
        while end < len(code) and depth:
            depth += (code[end] == "{") - (code[end] == "}")
            end += 1
        spans.append((begin, end, m[1]))
    return spans


def classify(api):
    if api in CREATE:
        return "resource creation"
    if api in DELETE:
        return "resource creation / release"
    if api in UPLOAD:
        return "resource upload"
    if api in PROGRAM_BUILD:
        return "resource creation"
    if api in RESOURCE_SETUP:
        return "resource creation / upload"
    if api in DRAW:
        return "draw submission"
    if api in FRAME:
        return "frame lifecycle"
    if api in READBACK or api.startswith("glGet") or api.startswith("glRead") or api.startswith("glIs"):
        return "readback / capability query"
    return "pipeline state"


def fmt_owners(rows):
    out = []
    for (path, owner), lines in sorted(rows.items()):
        locations = ", ".join(f"{line}" for line in sorted(lines))
        out.append(f"`{path}`: `{owner}` ({locations})")
    return "; ".join(out) or "—"


def object_owners(api, direct, indirect):
    rows = defaultdict(set)
    for (path, owner), lines in direct.get(api, {}).items():
        rows[(path, owner)].update(lines)
    for (path, owner), lines in indirect.get(api, {}).items():
        rows[(path, owner + " [indirect lookup]")].update(lines)
    return fmt_owners(rows)


def generate(revision):
    paths = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", revision, "src"], cwd=ROOT, text=True
    ).splitlines()
    uses = defaultdict(lambda: defaultdict(set))
    indirect = defaultdict(lambda: defaultdict(set))
    for path in paths:
        if Path(path).suffix not in {".c", ".h", ".cpp"} or "generated_" in path:
            continue
        raw = subprocess.check_output(["git", "show", f"{revision}:{path}"], cwd=ROOT, text=True)
        code = scrub(raw)
        spans = function_spans(code)
        for m in CALL.finditer(code):
            api = m[1]
            owners = [(end - begin, name) for begin, end, name in spans if begin <= m.start() < end]
            owner = min(owners)[1] if owners else "file/macro"
            line = code.count("\n", 0, m.start()) + 1
            uses[api][(path, owner)].add(line)
        for m in PROC_LOOKUP.finditer(raw):
            owners = [(end - begin, name) for begin, end, name in spans if begin <= m.start() < end]
            owner = min(owners)[1] if owners else "file/macro"
            line = raw.count("\n", 0, m.start()) + 1
            indirect[m[1]][(path, owner)].add(line)

    all_apis = sorted(uses)
    out = [
        "# Direct OpenGL call inventory",
        "",
        f"Audited source revision: `{revision}`.",
        "",
        "Generated by `python3 tools/renderer_audit.py --revision " + revision + "`.",
        "This lexical inventory includes conditional code and GL calls inside source macros;",
        "it is not a runtime reachability analysis. Line references are from the pinned",
        "revision. SDL context creation/swap and dynamically loaded GL entry points are",
        "listed separately. Calls are grouped by API name and every source owner is shown.",
        "Coverage is `src/**`; the isolated `platform/vita/**` bootstrap and host tools are",
        "outside this engine call inventory.",
        "",
        f"Direct GL API names: {len(all_apis)}. Source call sites (unique API/file/function/line): "
        f"{sum(len(lines) for api in uses for lines in uses[api].values())}.",
        "",
        "## Calls by API name and responsibility",
        "",
        "| API | Responsibility | Owner(s): source file, function, line(s) |",
        "| --- | --- | --- |",
    ]
    for api in all_apis:
        out.append(f"| `{api}` | {classify(api)} | {fmt_owners(uses[api])} |")

    out += ["", "## GL object creation and release owners", "",
            "The pairings below match object-generating and object-deleting API families.",
            "Owner functions are the actual source sites; container ownership and cross-call",
            "lifetimes are summarized in `RENDERER_CONTRACT.md`.", "",
            "| Object | Create API and owner(s) | Release API and owner(s) |", "| --- | --- | --- |"]
    for kind, create, delete in OBJECTS:
        created = object_owners(create, uses, indirect)
        released = object_owners(delete, uses, indirect)
        if kind == "query" and released == "—":
            released = "no explicit delete call; reclaimed with the owning GL context on teardown"
        out.append(f"| {kind} | `{create}` — {created} | `{delete}` — {released} |")

    out += ["", "## Dynamically loaded GL entry points", "",
            "These names are requested through `SDL_GL_GetProcAddress`, so they are not direct",
            "link-time call sites. They support optional GPU timer queries and have a CPU timing",
            "fallback when the complete extension is unavailable.", "",
            "| API | Responsibility | Lookup owner(s) |", "| --- | --- | --- |"]
    for api in sorted(indirect):
        out.append(f"| `{api}` | {classify(api)} | {fmt_owners(indirect[api])} |")
    out += ["", "## Coverage notes", "",
            "- API classification is by operation name; binding/state calls do not transfer",
            "  ownership. A `glDelete*` site identifies release code, while actual lifetime can",
            "  span initialization, scene replacement, and shutdown paths.",
            "- `glGetError` and object/status queries are grouped as readback/capability queries",
            "  even when used only to validate creation.",
            "- `glPolygonMode` is a desktop-only diagnostic path and is excluded from GLES builds.",
            "- SDL window/context creation and `SDL_GL_SwapWindow` are platform/frame-boundary",
            "  operations, not GL entry points; inspect `src/main.c` and `src/render.h`.", ""]
    return "\n".join(out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", default=DEFAULT_REVISION)
    parser.add_argument("--output", type=Path, default=ROOT / "docs/RENDERER_CALL_INVENTORY.md")
    args = parser.parse_args()
    args.output.write_text(generate(args.revision))
    print(args.output)
