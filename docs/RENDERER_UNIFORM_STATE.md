# Renderer uniform state cache

All OpenUG2 uniform reads and writes use the explicit wrappers declared in
`src/render.h`. `render_uniform.c` is the GL adapter and owns a bounded CPU
mirror so save/restore helpers do not depend on `glGetUniformfv`, which is not
available in vitaGL.

The mirror has 8 program slots, 128 uniform locations and 128 registered names
per program, 64 bytes per name, and 16 floats per location. A program slot is
keyed by its `GLuint`, so copying an `RProg` does not copy or fork its uniform
state. Program deletion clears the slot before that numeric name can be reused.
Capacity overflow, name-length errors, unknown programs or locations, and
unsupported cardinalities print a diagnostic to stderr and abort before a GL
uniform write can proceed; the cache never returns a fabricated zero after a
state tracking failure.

OpenGL initializes float, integer, vector, sampler, and matrix uniforms to
zero. Therefore a registered location that has not been written returns zero
from `render_uniform_getfv`. Location `-1` is the optimized-out sentinel: the
getter returns controlled zeros and setters do nothing. Other signed location
values are treated as opaque backend handles when they were registered by
`render_uniform_location`. Callers state the number of floats to save (1, 3, 4,
or 16 in current engine call sites); unsupported widths fail explicitly.

Uniform array writes with count greater than one are sent to GL as one upload.
The mirror resolves each element name (`name[index]`, starting at the registered
location's actual array index) with `glGetUniformLocation` and stores each value
at that returned location. It does not assume that array element locations are
adjacent. Both active and optimized-out element lookups are cached by name, so
repeated uploads do not query GL again. This requires a GL adapter that supports
the native indexed-array name form; the vitaGL package used for runtime must be
built with its strict uniform-name support enabled. Call
`render_uniform_location(program, name)` for every engine location so array
writes can recover the name and starting index.

The renderer currently owns GL program binding on one thread. Every engine
`glUseProgram` operation, including the optional frontend and debug overlay,
must use `render_use_program`; the adapter tracks that binding to associate
setter calls with their program. Keep future uniform calls and program deletion
on this path. The cache has no locking and is not intended for concurrent GL
contexts.

`tools/render_uniform_cache_test.c` supplies stub GL entry points, including
noncontiguous and partially optimized-out array locations, to test
program/location collisions, defaults, the `-1` sentinel and opaque negative
handles, indexed array starts and aliases, cached query counts, capacity
faults, scalar/vector/matrix writes, and deletion.
`tools/render_uniform_live_test.c` creates a host SDL GL context and compares
wrapper save/restore values against `glGetUniformfv`; that query is test-only.
