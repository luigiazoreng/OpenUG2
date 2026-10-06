# VitaGL shader swizzle-name compatibility

The VitaGL GLSL translator header defines `t` as an alias for the `y`
component. The renderer's fragment shader had a `vec4 t` texture sample and a
nested `float y` luminance variable. After VitaGL preprocessing, the sample is
named `y`, so the luminance declaration shadows it and `y.rgb` in the initializer
attempts a vector swizzle on the scalar being declared. The rim luminance local
is now named `luma`; its coefficients and paint calculation are unchanged.

`tools/render_swizzle_macro_test.c` injects all six component aliases from the
pinned translator after `#version` and calls production `render_program`. Its
approved test asserts fragment compile and program link success on host GL and
GLES. It does not assert that the old shader fails on those drivers: GLSL
initializer scope differs from the Cg behavior shown by the device log.

Exploratory baseline runs against integration-development are preserved
separately; Mesa llvmpipe GL 4.5 and GLES 3.2 compile and link that old shader,
so those runs are not approved tests and do not reproduce the device failure.
The captured full-shader preprocessing shows the collision (`vec4 y` followed
by `float y=dot(y.rgb,...)`); the fixed source preserves the `vec4 y` and
computes through `luma`. The fixed shader passes on both host APIs. The expanded
suite of 19 GPU probes also passes on each API; four raw RGBA checks supplement
the original 15 probes without replacing them.

Approved final fixture logs and command/hash evidence for the bounded
validation are kept outside the source tree under
`reports/vita-item5-2026-10-06/gl-compat/shader/final-six-aliases/`. The older
exploratory baseline runs remain in the sibling `shader/` directory.
