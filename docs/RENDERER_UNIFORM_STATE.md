# Renderer uniform state cache

All OpenUG2 uniform reads and writes use the explicit wrappers declared in
`src/render.h`. `render_uniform.c` is the GL adapter and owns a bounded CPU
mirror so save/restore helpers do not depend on `glGetUniformfv`, which is not
available in vitaGL.

The mirror has 8 program slots, 128 uniform locations per program, 64 bytes
per registered uniform name, and 16 floats per location. A program slot is
keyed by its `GLuint`, so copying an `RProg` does not copy or fork its uniform
state. Program deletion clears the slot before that numeric name can be reused.
Capacity, name-length, and unregistered-array failures print a diagnostic to
stderr. They do not silently pretend that a write was cached.

OpenGL initializes float, integer, vector, sampler, and matrix uniforms to
zero. Therefore a valid location that has not been written returns zero from
`render_uniform_getfv`; a negative optimized-out location also returns a
controlled zero and setters ignore it. Callers state the number of floats to
save (1, 3, 4, or 16 in current engine call sites). Values wider than 16 floats
are outside this cache contract.

Uniform array writes with count greater than one are sent to GL as one upload.
The mirror resolves each element name (`name[0]`, `name[1]`, and so on) with
`glGetUniformLocation` and stores each value at that returned location. It does
not assume that array element locations are adjacent. Call
`render_uniform_location(program, name)` for locations used by a setter or
getter so array writes can recover the element names.

The renderer currently owns GL program binding on one thread. Every engine
`glUseProgram` operation, including the optional frontend and debug overlay,
must use `render_use_program`; the adapter tracks that binding to associate
setter calls with their program. Keep future uniform calls and program deletion
on this path. The cache has no locking and is not intended for concurrent GL
contexts.

`tools/render_uniform_cache_test.c` supplies stub GL entry points, including
noncontiguous array locations, to test program/location collisions, defaults,
negative locations, scalar/vector/matrix writes, array writes, and deletion.
`tools/render_uniform_live_test.c` creates a host SDL GL context and compares
wrapper save/restore values against `glGetUniformfv`; that query is test-only.
